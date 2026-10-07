#pragma once
// A retained 3D scene (Stage) as a Scenery: loads its archive and builds its fixed
// layers off the UI thread, sways its foliage on the governed cadence, and startles
// its creatures when the glass is tapped. Stillwater is a Diorama.
//
// A diorama may offer several scenes (tanks, say). The player moves between them
// with the Scene command; the choice is remembered in the settings ("scene <id>").
// The next scene loads and builds on workers while the current one keeps playing
// and dims; it fades in once its first picture is ready.
#include "archive.hpp"
#include "motion.hpp"
#include "scenery.hpp"
#include "stage.hpp"

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace ambient {

// Creates the scene's look once its archive has loaded (on a worker thread).
using LookFactory = std::unique_ptr<Look> (*)(const SceneData& scene);
// Adds a game's own scenery to a loaded archive before anything is built from it (a
// prop the archive does not carry). Runs on the loading worker.
using SceneDressing = void (*)(SceneData& scene);
#ifdef GUI_FORMS_AUDIO_GENERATOR
// Creates a voice the scene synthesizes as it plays.
using VoiceFactory = std::shared_ptr<gui_forms::AudioGenerator> (*)();

// A live sound bed that answers its diorama: it hears which scene is shown and the
// taps on the glass. Both calls come from the UI thread while the device renders on
// its own, so an implementation hands them over without locking.
class SceneVoice : public gui_forms::AudioGenerator {
  public:
    // The scene now shown (an index into the diorama's scenes). The first call comes
    // before the voice is first heard.
    virtual void show_scene(std::size_t) noexcept {}
    // A tap on the glass at x, y (fractions of the view) that startled `startled`
    // creatures. True when the voice plays the tap itself; the tap clip is then not played.
    virtual bool tap(double, double, std::size_t) noexcept {
        return false;
    }
};
using SceneVoiceFactory = std::shared_ptr<SceneVoice> (*)();
#endif

// One scene a diorama can show.
struct DioramaScene {
    std::string id;         // saved in the settings; a single word
    std::string name;       // shown in the Scene command
    std::string archive;    // below the game's asset folder, e.g. "scene/riverscape.ambient"
    LookFactory make_look{};
    SceneDressing dress{};
    Bounds bounds{};        // where startled creatures may go
};

struct DioramaSetup {
    std::string id;         // the game id: archives are read from <assets>/<id>/
    std::string archive;    // a single scene's archive (when `scenes` is empty)
    std::string ambience;   // looping sound bed stem (the Sound master); empty for none
    std::string music;      // looping music stem (the Music master); empty for none
    std::string tap_sound;  // effect stem for a tap on the glass; empty for none
    LookFactory make_look{};
    SceneDressing dress{};
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // Live voices, preferred to the clips above where the toolkit can play them.
    SceneVoiceFactory live_ambience{};  // the Sound master, like the bed
    VoiceFactory live_music{};     // the Music master
#endif
    Bounds bounds{};        // where startled creatures may go
    // Several scenes to choose between; the first is the default. When empty, the
    // single scene above (archive, make_look, dress, bounds) is the only one.
    std::vector<DioramaScene> scenes{};
};

class Diorama final : public Scenery {
  public:
    explicit Diorama(DioramaSetup setup);
    ~Diorama() override;
    Diorama(const Diorama&) = delete;
    Diorama& operator=(const Diorama&) = delete;

    bool poll(SceneContext& context) override;
    [[nodiscard]] bool ready() const override;
    [[nodiscard]] std::string failure() const override;
    void resize(SceneSize size, bool settled) override;
    void advance(double seconds, SceneContext& context) override;
    const std::vector<std::uint32_t>& draw(SceneContext& context, int& width, int& height) override;
    void press(double x, double y, SceneContext& context) override;
    void add_settings(std::vector<games::GameSetting>& list, const Settings& settings) const override;
    bool change_setting(std::string_view id, double value, SceneContext& context) override;
    bool run_command(std::string_view id, SceneContext& context) override;
    [[nodiscard]] std::string key_command(std::uint32_t key) const override;
    bool scripted_action(std::string_view action, SceneContext& context) override;
    void sound(bool running, SceneContext& context) override;

    // The scenes on offer and the one shown (or being faded to).
    [[nodiscard]] const std::vector<DioramaScene>& scenes() const {
        return scenes_;
    }
    [[nodiscard]] std::size_t chosen() const {
        return chosen_;
    }
    // True while a change of scene is under way.
    [[nodiscard]] bool changing() const {
        return fade_ != Fade::none || loading_.valid() || next_building_.valid() || next_.stage != nullptr;
    }

  private:
    struct Bundle {
        SceneData scene{};
        std::unique_ptr<Look> look{};
        ShadowMap shadows{};
        Foliage foliage{};
        std::string error{};
    };
    // A loaded scene and its stage.
    struct Showing {
        std::unique_ptr<Bundle> bundle{};  // must outlive stage
        std::unique_ptr<Stage> stage{};
        std::vector<Creature> creatures{};
        std::size_t scene{};
    };
    enum class Fade : std::uint8_t { none, out, in };
    static std::unique_ptr<Bundle> load_bundle(std::filesystem::path archive, LookFactory make_look,
                                               SceneDressing dress);
    void start_loading(std::size_t scene);
    void start_build();
    void finish_sway(SceneContext* context);
    static double sway_job(Stage* stage, double time);
    void tap(double x, double y, SceneContext& context);
    void choose(std::size_t scene, SceneContext& context);
    void swap_in();
    [[nodiscard]] double fade_level() const;

    DioramaSetup setup_;
    std::vector<DioramaScene> scenes_{};
    bool started_{};
    std::size_t chosen_{};  // the scene saved and shown, or being faded to
    Showing shown_{};
    // The next scene: loading, then its first fixed layer, while the shown one plays.
    std::future<std::unique_ptr<Bundle>> loading_{};
    std::size_t loading_scene_{};
    Showing next_{};
    std::future<FixedLayer> next_building_{};
    SceneSize next_size_{};
    std::string error_{};
    // The fade between scenes, timed on the wall clock so it also runs while paused.
    Fade fade_{Fade::none};
    std::chrono::steady_clock::time_point fade_start_{};
    double fade_from_{1};
    std::vector<std::uint32_t> faded_{};

    std::future<FixedLayer> building_{};
    SceneSize building_size_{};
    SceneSize wanted_{};
    bool settled_{};
    // The foliage is redrawn on a worker into the stage's hidden sway layer while
    // frames keep composing over the shown one; the result is the worker's seconds.
    std::future<double> swaying_{};
    double light_time_{};   // the animated light's clock; held under reduced motion
    double last_sway_{-1};  // scene time of the last foliage update
    std::vector<std::uint32_t> empty_{};
#ifdef GUI_FORMS_AUDIO_GENERATOR
    std::shared_ptr<SceneVoice> ambience_voice_{};
    std::shared_ptr<gui_forms::AudioGenerator> music_voice_{};
    std::size_t voiced_scene_{static_cast<std::size_t>(-1)};  // the scene the voice was last told of
#endif
};

} // namespace ambient
