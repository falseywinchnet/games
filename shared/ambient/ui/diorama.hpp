#pragma once
// A retained 3D scene (Stage) as a Scenery: loads its archive and builds its fixed
// layers off the UI thread, sways its foliage on the governed cadence, and startles
// its creatures when the glass is tapped. Stillwater is a Diorama.
#include "archive.hpp"
#include "motion.hpp"
#include "scenery.hpp"
#include "stage.hpp"

#include <filesystem>
#include <future>
#include <memory>
#include <string>

namespace ambient {

// Creates the scene's look once its archive has loaded (on a worker thread).
using LookFactory = std::unique_ptr<Look> (*)(const SceneData& scene);
// Adds a game's own scenery to a loaded archive before anything is built from it (a
// prop the archive does not carry). Runs on the loading worker.
using SceneDressing = void (*)(SceneData& scene);
#ifdef GUI_FORMS_AUDIO_GENERATOR
// Creates a voice the scene synthesizes as it plays.
using VoiceFactory = std::shared_ptr<gui_forms::AudioGenerator> (*)();
#endif

struct DioramaSetup {
    std::string id;         // the game id: the archive is read from <assets>/<id>/
    std::string archive;    // below the game's asset folder, e.g. "scene/riverscape.ambient"
    std::string ambience;   // looping sound bed stem (the Sound master); empty for none
    std::string music;      // looping music stem (the Music master); empty for none
    std::string tap_sound;  // effect stem for a tap on the glass; empty for none
    LookFactory make_look{};
    SceneDressing dress{};
#ifdef GUI_FORMS_AUDIO_GENERATOR
    // Live voices, preferred to the clips above where the toolkit can play them.
    VoiceFactory live_ambience{};  // the Sound master, like the bed
    VoiceFactory live_music{};     // the Music master
#endif
    Bounds bounds{};        // where startled creatures may go
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
    bool scripted_action(std::string_view action, SceneContext& context) override;
    void sound(bool running, SceneContext& context) override;

  private:
    struct Bundle {
        SceneData scene{};
        std::unique_ptr<Look> look{};
        ShadowMap shadows{};
        Foliage foliage{};
        std::string error{};
    };
    static std::unique_ptr<Bundle> load_bundle(std::filesystem::path archive, LookFactory make_look,
                                               SceneDressing dress);
    void start_build();
    void finish_sway(SceneContext* context);
    static double sway_job(Stage* stage, double time);
    void tap(double x, double y, SceneContext& context);

    DioramaSetup setup_;
    std::future<std::unique_ptr<Bundle>> loading_{};
    std::unique_ptr<Bundle> bundle_{};  // must outlive stage_
    std::future<FixedLayer> building_{};
    SceneSize building_size_{};
    SceneSize wanted_{};
    bool settled_{};
    std::unique_ptr<Stage> stage_{};
    // The foliage is redrawn on a worker into the stage's hidden sway layer while
    // frames keep composing over the shown one; the result is the worker's seconds.
    std::future<double> swaying_{};
    std::vector<Creature> creatures_{};
    double light_time_{};   // the animated light's clock; held under reduced motion
    double last_sway_{-1};  // scene time of the last foliage update
    std::vector<std::uint32_t> empty_{};
#ifdef GUI_FORMS_AUDIO_GENERATOR
    std::shared_ptr<gui_forms::AudioGenerator> ambience_voice_{};
    std::shared_ptr<gui_forms::AudioGenerator> music_voice_{};
#endif
};

} // namespace ambient
