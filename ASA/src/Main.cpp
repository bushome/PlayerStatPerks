// PlayerStatPerks - player base stats and per-level stat gains by permission group.
//
// Groups come from the Permissions plugin and can be permanent, tribe or timed groups. Each group in
// config.json sets per-level stat multipliers and base stats; a player's groups stack by priority.
// /perks shows a player what they currently get.
//
// There are no gameplay hooks. AsaApi's one-second timer drives a scan of the online players every
// PollInterval_Seconds. A player's stats are (re)applied when
//   * a new character body shows up (login, respawn), after SpawnSettle_Seconds,
//   * Permissions reports that their groups changed (e.g. permissions.addtimed), on the next poll,
//   * their resolved group set differs on the periodic recheck every GroupRecheck_Seconds (this is
//     what catches a timed group expiring),
//   * the game has overwritten the values this plugin wrote (fingerprint mismatch).
// Values are always computed from the class defaults and the server's stat multipliers, never from
// the live component, so re-applying can never compound and nothing stale survives a save/load.
//
// Everything here runs on the game thread (the timer callback and the chat/console/RCON commands),
// except the Permissions group-change callback, which only touches the mutex-guarded change list.

#include "API/ARK/Ark.h"

#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PermissionsApi.h"
#include "PerkLogic.h"

#ifndef PROJECT_NAME
#define PROJECT_NAME "PlayerStatPerks"
#endif

namespace
{
	using namespace StatPerks;

	constexpr const char* kReloadCommand = PROJECT_NAME ".Reload";
	constexpr const char* kTimerId = PROJECT_NAME "_Timer";

	Settings g_settings;
	bool g_settings_loaded = false;
	std::string g_registered_chat_command;
	long long g_clock = 0;   // seconds since load, advanced by the timer callback

	struct PlayerState
	{
		AShooterCharacter* character = nullptr;
		long long first_seen = 0;
		long long last_group_check = 0;
		bool applied = false;
		bool seen_this_pass = false;
		std::string signature;
		Effective effective;
		std::array<float, StatCount> expected_gain{};
		std::array<float, StatCount> expected_base{};
	};
	std::unordered_map<std::string, PlayerState> g_players;   // key: EOS ID

	// Group-change notifications from Permissions. Permissions may call back from its own worker
	// thread, so the callback only records who changed; the next poll (game thread) acts on it.
	constexpr const char* kGroupCallbackId = PROJECT_NAME "_GroupUpdated";
	bool g_group_callback_subscribed = false;
	std::mutex g_changed_mutex;
	std::unordered_set<std::string> g_changed_players;
	bool g_changed_everyone = false;

	void OnPermissionGroupUpdated(const FString& eos_id, int /*tribe_id*/)
	{
		std::lock_guard<std::mutex> lock(g_changed_mutex);
		if (eos_id.IsEmpty())
			g_changed_everyone = true;   // a tribe's groups changed; recheck all online players
		else
			g_changed_players.insert(eos_id.ToString());
	}

	// Permissions.dll is delay-loaded and AsaApi may load this plugin first, so nothing may call into
	// it until it is actually in the process.
	bool PermissionsLoaded()
	{
		return GetModuleHandleW(L"Permissions.dll") != nullptr;
	}

	void SubscribeGroupCallback()
	{
		if (g_group_callback_subscribed || !PermissionsLoaded())
			return;
		Permissions::SubscribePermissionGroupUpdatedCallback(kGroupCallbackId, &OnPermissionGroupUpdated);
		g_group_callback_subscribed = true;
		Log::GetLog()->info("Listening for Permissions group changes");
	}

	std::string JoinGroups(const Effective& eff)
	{
		std::string out;
		for (const std::string& group : eff.matched_groups)
		{
			if (!out.empty()) out += ", ";
			out += group;
		}
		return out;
	}

	FString ToFString(const std::string& text)
	{
		return FString(AsaApi::Tools::Utf8Decode(text));
	}

	// ------------------------------------------------------------------------------------------
	// Config
	// ------------------------------------------------------------------------------------------

	Settings LoadSettings()
	{
		const std::string path = AsaApi::Tools::GetCurrentDir() + "/ArkApi/Plugins/" PROJECT_NAME "/config.json";

		std::ifstream file(path);
		if (!file.is_open())
			throw std::runtime_error("cannot open " + path);

		nlohmann::json root;
		try
		{
			// Last argument: allow // and /* */ comments in config.json.
			root = nlohmann::json::parse(file, nullptr, true, true);
		}
		catch (const nlohmann::json::exception& error)
		{
			throw std::runtime_error(std::string("config.json is not valid JSON: ") + error.what());
		}

		return ParseSettings(root);
	}

	void PerksChat(AShooterPlayerController* player_controller, FString* message, int mode, int platform);

	void RegisterChatCommand()
	{
		if (g_registered_chat_command == g_settings.perks_command)
			return;
		if (!g_registered_chat_command.empty())
			AsaApi::GetCommands().RemoveChatCommand(ToFString(g_registered_chat_command));
		AsaApi::GetCommands().AddChatCommand(ToFString(g_settings.perks_command), &PerksChat);
		g_registered_chat_command = g_settings.perks_command;
	}

	bool ReloadSettings(std::string& error)
	{
		try
		{
			Settings loaded = LoadSettings();

			for (const std::string& warning : loaded.warnings)
				Log::GetLog()->warn("Config: {}", warning);

			Log::GetLog()->info("Config loaded: {} permission group(s), stacking {}, poll every {}s, group recheck every {}s",
				loaded.groups.size(), loaded.stack_groups ? "on" : "off",
				loaded.poll_interval_seconds, loaded.group_recheck_seconds);

			g_settings = std::move(loaded);
			g_settings_loaded = true;
			RegisterChatCommand();

			// Force every online player to be recalculated with the new settings on the next poll.
			for (auto& [id, state] : g_players)
			{
				state.applied = false;
				state.signature.clear();
			}
			return true;
		}
		catch (const std::exception& exception)
		{
			error = exception.what();
			return false;
		}
	}

	// ------------------------------------------------------------------------------------------
	// Groups
	// ------------------------------------------------------------------------------------------

	std::vector<std::string> GetGroups(const FString& eos_id)
	{
		std::vector<std::string> out;
		const TArray<FString> groups = Permissions::GetPlayerGroups(eos_id);
		for (const FString& group : groups)
			out.push_back(group.ToString());
		return out;
	}

	// ------------------------------------------------------------------------------------------
	// Applying stats
	// ------------------------------------------------------------------------------------------

	float* GameModeStatArray(const char* field)
	{
		AShooterGameMode* game_mode = AsaApi::GetApiUtils().GetShooterGameMode();
		return game_mode != nullptr ? GetNativePointerField<float*>(game_mode, field) : nullptr;
	}

	UPrimalCharacterStatusComponent* StatusOf(AShooterCharacter* character)
	{
		return character != nullptr ? character->MyCharacterStatusComponentField() : nullptr;
	}

	// True when the live component still holds what this plugin last wrote.
	bool FingerprintMatches(UPrimalCharacterStatusComponent* status, const PlayerState& state)
	{
		const float* gain = status->AmountMaxGainedPerLevelUpValueField()();
		const float* base = status->BaseLevelMaxStatusValuesField()();
		for (int s = 0; s < StatCount; ++s)
		{
			const int i = Stats()[s].game_index;
			if (gain[i] != state.expected_gain[s] || base[i] != state.expected_base[s])
				return false;
		}
		return true;
	}

	bool Apply(AShooterCharacter* character, PlayerState& state)
	{
		UPrimalCharacterStatusComponent* status = StatusOf(character);
		if (status == nullptr)
			return false;

		UClass* status_class = status->ClassPrivateField();
		auto* defaults = status_class != nullptr
			? static_cast<UPrimalCharacterStatusComponent*>(status_class->GetDefaultObject(true))
			: nullptr;
		const float* ini_per_level = GameModeStatArray("AShooterGameMode.PerLevelStatsMultiplier_Player");
		const float* ini_base = GameModeStatArray("AShooterGameMode.PlayerBaseStatMultipliers");
		if (defaults == nullptr || ini_per_level == nullptr || ini_base == nullptr)
			return false;

		const float* default_max = defaults->MaxStatusValuesField()();
		const float* default_gain = defaults->AmountMaxGainedPerLevelUpValueField()();

		float* max_values = status->MaxStatusValuesField()();
		float* base_values = status->BaseLevelMaxStatusValuesField()();
		float* gain_values = status->AmountMaxGainedPerLevelUpValueField()();
		float* current_values = status->CurrentStatusValuesField()();
		const unsigned char* points = status->NumberOfLevelUpPointsAppliedField()();

		const Effective& eff = state.effective;
		for (int s = 0; s < StatCount; ++s)
		{
			const StatInfo& info = Stats()[s];
			const int i = info.game_index;

			const double vanilla_base = static_cast<double>(default_max[i]) * ini_base[i];
			const double base = eff.base_stats[s]
				? ToEngineBase(static_cast<Stat>(s), *eff.base_stats[s], default_max[i])
				: vanilla_base;
			const double gain = static_cast<double>(default_gain[i]) * eff.multipliers[s];
			const double new_max = base + static_cast<double>(points[i]) * gain * ini_per_level[i];

			const float old_max = max_values[i];
			base_values[i] = static_cast<float>(base);
			gain_values[i] = static_cast<float>(gain);   // so the game's own level-ups use the new gain
			max_values[i] = static_cast<float>(new_max);

			if (info.scales_current)
				current_values[i] = old_max > 0.f ? current_values[i] / old_max * max_values[i] : max_values[i];
			else if (i != EPrimalCharacterStatusValue::Weight)
				current_values[i] = max_values[i];   // speed, crafting, fortitude

			state.expected_gain[s] = gain_values[i];
			state.expected_base[s] = base_values[i];
		}

		status->UpdateWeightStat(false);
		status->ServerSyncReplicatedValues();

		TArray<float> net_max;
		TArray<float> net_base;
		for (int i = 0; i < EPrimalCharacterStatusValue::MAX; ++i)
		{
			net_max.Add(max_values[i]);
			net_base.Add(base_values[i]);
		}
		status->NetSyncMaxStatusValues(&net_max, &net_base);
		return true;
	}

	// ------------------------------------------------------------------------------------------
	// Polling
	// ------------------------------------------------------------------------------------------

	void Poll()
	{
		UWorld* world = AsaApi::GetApiUtils().GetWorld();
		if (world == nullptr)
			return;

		for (auto& [id, state] : g_players)
			state.seen_this_pass = false;

		// Take the group-change notifications collected since the last poll.
		std::unordered_set<std::string> changed;
		bool changed_everyone = false;
		{
			std::lock_guard<std::mutex> lock(g_changed_mutex);
			changed.swap(g_changed_players);
			changed_everyone = g_changed_everyone;
			g_changed_everyone = false;
		}

		for (TWeakObjectPtr<APlayerController> weak_controller : world->PlayerControllerListField())
		{
			auto* player_controller = static_cast<AShooterPlayerController*>(weak_controller.Get());
			if (player_controller == nullptr)
				continue;

			AShooterCharacter* character = player_controller->GetPlayerCharacter();
			if (character == nullptr || character->IsDead())
				continue;

			const FString eos_id = AsaApi::IApiUtils::GetEOSIDFromController(player_controller);
			if (eos_id.IsEmpty())
				continue;

			PlayerState& state = g_players[eos_id.ToString()];
			state.seen_this_pass = true;

			if (state.character != character)   // login or respawn
			{
				state = PlayerState{};
				state.character = character;
				state.first_seen = g_clock;
				state.seen_this_pass = true;
			}
			if (g_clock - state.first_seen < g_settings.spawn_settle_seconds)
				continue;

			UPrimalCharacterStatusComponent* status = StatusOf(character);
			if (status == nullptr)
				continue;

			bool need_apply = !state.applied || !FingerprintMatches(status, state);
			const bool groups_changed = changed_everyone || changed.count(eos_id.ToString()) != 0;

			if (need_apply || groups_changed || g_clock - state.last_group_check >= g_settings.group_recheck_seconds)
			{
				state.last_group_check = g_clock;
				Effective eff = Resolve(g_settings, GetGroups(eos_id));
				if (eff.Signature() != state.signature)
					need_apply = true;
				state.effective = std::move(eff);
				state.signature = state.effective.Signature();
			}

			if (need_apply)
			{
				state.applied = Apply(character, state);
				if (state.applied)
					Log::GetLog()->info("Applied [{}] to {}", JoinGroups(state.effective), eos_id.ToString());
			}
		}

		// Forget players who left.
		for (auto it = g_players.begin(); it != g_players.end();)
			it = it->second.seen_this_pass ? std::next(it) : g_players.erase(it);
	}

	void OnTimer()
	{
		++g_clock;
		if (!g_settings_loaded || g_clock % g_settings.poll_interval_seconds != 0)
			return;

		try
		{
			if (!PermissionsLoaded())
				return;   // nothing to resolve groups with yet
			SubscribeGroupCallback();
			Poll();
		}
		catch (const std::exception& exception)
		{
			Log::GetLog()->error("Stat update failed: {}", exception.what());
		}
	}

	// ------------------------------------------------------------------------------------------
	// Commands
	// ------------------------------------------------------------------------------------------

	void PerksChat(AShooterPlayerController* player_controller, FString* /*message*/, int /*mode*/, int /*platform*/)
	{
		if (player_controller == nullptr || !g_settings_loaded || !PermissionsLoaded())
			return;

		const FString eos_id = AsaApi::IApiUtils::GetEOSIDFromController(player_controller);
		const Effective eff = Resolve(g_settings, GetGroups(eos_id));
		const FString text = ToFString(FormatPerks(g_settings.perks_message.text, eff));
		const Message& message = g_settings.perks_message;

		if (message.send_in_chat)
			AsaApi::GetApiUtils().SendChatMessage(player_controller, ToFString(g_settings.sender), L"{}", *text);
		else
			AsaApi::GetApiUtils().SendNotification(player_controller, FLinearColor(message.r, message.g, message.b, 1.f),
				1.f, 10.f, nullptr, L"{}", *text);
	}

	void ReloadConsole(APlayerController* player_controller, FString* /*command*/, bool /*write_to_log*/)
	{
		auto* shooter_controller = static_cast<AShooterPlayerController*>(player_controller);
		if (shooter_controller == nullptr || !shooter_controller->bIsAdmin()())
			return;

		std::string error;
		if (ReloadSettings(error))
		{
			AsaApi::GetApiUtils().SendServerMessage(shooter_controller, FColorList::Green, "PlayerStatPerks: config reloaded");
		}
		else
		{
			Log::GetLog()->error("Config reload failed: {}", error);
			AsaApi::GetApiUtils().SendServerMessage(shooter_controller, FColorList::Red, "PlayerStatPerks: reload failed - {}", error);
		}
	}

	void ReloadRcon(RCONClientConnection* rcon_connection, RCONPacket* rcon_packet, UWorld* /*world*/)
	{
		FString reply;

		std::string error;
		if (ReloadSettings(error))
		{
			reply = "PlayerStatPerks: config reloaded";
		}
		else
		{
			Log::GetLog()->error("Config reload failed: {}", error);
			reply = FString("PlayerStatPerks: reload failed - " + error);
		}

		rcon_connection->SendMessageW(rcon_packet->Id, 0, &reply);
	}
} // namespace

// Called by AsaApi when the plugin is loaded.
extern "C" __declspec(dllexport) void Plugin_Init()
{
	Log::Get().Init(PROJECT_NAME);

	std::string error;
	if (!ReloadSettings(error))
	{
		Log::GetLog()->error("Config not loaded: {}", error);
		Log::GetLog()->error("Player stats are left alone until config.json is fixed and {} is run", kReloadCommand);
	}

	AsaApi::GetCommands().AddConsoleCommand(kReloadCommand, &ReloadConsole);
	AsaApi::GetCommands().AddRconCommand(kReloadCommand, &ReloadRcon);
	AsaApi::GetCommands().AddOnTimerCallback(kTimerId, &OnTimer);
}

// Called by AsaApi when the plugin is unloaded.
extern "C" __declspec(dllexport) void Plugin_Unload()
{
	AsaApi::GetCommands().RemoveOnTimerCallback(kTimerId);
	if (g_group_callback_subscribed && PermissionsLoaded())
		Permissions::UnSubscribePermissionGroupUpdatedCallback(kGroupCallbackId);
	g_group_callback_subscribed = false;
	AsaApi::GetCommands().RemoveConsoleCommand(kReloadCommand);
	AsaApi::GetCommands().RemoveRconCommand(kReloadCommand);
	if (!g_registered_chat_command.empty())
		AsaApi::GetCommands().RemoveChatCommand(ToFString(g_registered_chat_command));

	g_registered_chat_command.clear();
	g_players.clear();
}
