// The PlaySuite masters: their file, the move from the card cabinet that held them
// before, the copy kept in the cabinet for earlier versions, and observation.
#include "storage.hpp"
#include "suite_settings.hpp"
#include "test_paths.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
struct Counter {
    int* count;
    void operator()() const { ++*count; }
};
games::Cabinet old_cabinet() {
    games::Cabinet cabinet;
    cabinet.started[0] = true;
    cabinet.games[0].deal(games::Kind::solitaire, 7);
    cabinet.music = false;
    cabinet.sound = true;
    cabinet.reduced = true;
    cabinet.back = 2;
    return cabinet;
}
} // namespace
int main() {
    try {
        // Text round trip; unknown keys are ignored, damaged values refused.
        games::SuiteSettings chosen;
        chosen.music = false;
        chosen.reduced = true;
        chosen.music_volume = .35;
        chosen.sound_volume = .8;
        chosen.card_back = 3;
        games::SuiteSettings decoded;
        require(games::decode_suite_settings(games::encode_suite_settings(chosen), decoded) &&
                    decoded == chosen,
                "settings round trip");
        require(games::decode_suite_settings("PLAYSUITE_SETTINGS 2\nmusic=0\nlater_key=7\n", decoded) &&
                    !decoded.music && decoded.sound && decoded.music_volume == 1,
                "unknown keys are ignored and missing keys take defaults");
        games::SuiteSettings untouched = chosen;
        require(!games::decode_suite_settings("garbage", untouched) && untouched == chosen,
                "not settings: unchanged");
        require(!games::decode_suite_settings("PLAYSUITE_SETTINGS 1\nmusic_volume=140\n", untouched) &&
                    untouched == chosen,
                "out-of-range volume refused");
        require(!games::decode_suite_settings("PLAYSUITE_SETTINGS 1\ncard_back=9\n", untouched),
                "unknown card back refused");
        require(games::audio_gain(1) == 1 && games::audio_gain(0) == 0 &&
                    std::abs(games::audio_gain(.5) - .25) < 1e-12,
                "volume positions give squared gains");

        // A folder from before the settings file: the masters move over from the cabinet.
        const std::filesystem::path root = games_test::scratch_directory("games-settings-test-");
        games_test::isolate_saves(root);
        require(games::save_cabinet(games::cabinet_path(), old_cabinet()), "old cabinet");
        games::SettingsStore& store = games::SettingsStore::shared();
        store.reload();
        const games::SuiteSettings migrated = store.values();
        require(!migrated.music && migrated.sound && migrated.reduced && migrated.card_back == 2 &&
                    migrated.music_volume == 1 && migrated.sound_volume == 1,
                "masters and card back migrate from the cabinet");
        require(!std::filesystem::exists(games::suite_settings_path()),
                "reading alone writes nothing");

        // Changing a master saves the file, keeps the cabinet's copy, and tells observers.
        int changes = 0;
        {
            const games::SettingsObservation watching = store.observe(Counter{&changes});
            store.toggle_music();
            require(changes == 1 && store.values().music, "toggle notifies once");
            games::SuiteSettings next = store.values();
            store.set(next);
            require(changes == 1, "an unchanged value notifies nobody");
            next.sound_volume = .4;
            next.card_back = 1;
            store.set(next);
            require(changes == 2, "volume and card back notify");
        }
        store.toggle_sound();
        require(changes == 2, "a dropped observation is not called");
        games::Cabinet mirrored;
        require(games::load_cabinet(games::cabinet_path(), mirrored) && mirrored.music &&
                    !mirrored.sound && mirrored.reduced && mirrored.back == 1,
                "earlier versions read the masters from the cabinet");
        require(mirrored.started[0] && mirrored.games[0].state.seed == old_cabinet().games[0].state.seed,
                "the cabinet's games are untouched");

        // A later start reads the file, not the cabinet.
        mirrored.music = false;
        require(games::save_cabinet(games::cabinet_path(), mirrored), "older version writes cabinet");
        store.reload();
        require(store.values().music && std::abs(store.values().sound_volume - .4) < 1e-9 &&
                    store.values().card_back == 1,
                "the settings file wins once it exists");

        // Another state folder (tests, --dev) is another set of settings.
        const std::filesystem::path fresh = games_test::scratch_directory("games-settings-fresh-");
        games_test::isolate_saves(fresh);
        require(store.values() == games::SuiteSettings{}, "a fresh folder starts from defaults");
        std::filesystem::remove_all(root);
        std::filesystem::remove_all(fresh);
        std::cout << "Settings file, migration, cabinet copy and observation pass.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
