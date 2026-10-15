// Candidates of the falls' look, as stills for the owner to choose between: the sky,
// the sun and the crest, the part of the scene that sells the rest.
//
//   falls_candidates out_dir [width height rows samples time]
//
// Writes one PNG per candidate (A.png, B.png, ...) and a line per candidate of what
// the brightest and darkest parts measured, so exposure is judged, not guessed.
#include "still.hpp"
#include "png_writer.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

struct Candidate {
    const char* name;
    const char* label;
    cf::Look look;
    cf::Camera camera;
};

void report(const char* name, const cf::LinearImage& image) {
    double brightest = 0, total = 0;
    for (const cf::Rgb& p : image.pixels) {
        const double l = cf::luminance(p);
        brightest = std::max(brightest, l);
        total += l;
    }
    std::printf("%s: mean %.4f, brightest %.2f\n", name, total / static_cast<double>(image.pixels.size()), brightest);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s out_dir [width height rows samples time]\n", argv[0]);
        return 2;
    }
    const std::string directory = argv[1];
    const int width = argc > 2 ? std::atoi(argv[2]) : 1100;
    const int height = argc > 3 ? std::atoi(argv[3]) : 760;
    const int rows = argc > 4 ? std::atoi(argv[4]) : 440;
    const int samples = argc > 5 ? std::atoi(argv[5]) : 3;
    const double time = argc > 6 ? std::atof(argv[6]) : 3.7;
    if (width < 16 || height < 16 || rows < 1 || rows > height || samples < 1 || samples > 8)
        return 2;

    Candidate list[3]{};
    // A: a clear morning after rain, the sun high over the brink.
    list[0].name = "A";
    list[0].label = "clear morning";
    list[0].look.sky.sun_elevation = 46 * cf::pi / 180;
    list[0].look.sky.haze = 2.0;
    list[0].look.sky.haze_forward = .76;
    // B: the humid afternoon, a wide white glory round the sun.
    list[1].name = "B";
    list[1].label = "humid glory";
    list[1].look.sky.sun_elevation = 48 * cf::pi / 180;
    list[1].look.sky.haze = 6.5;
    list[1].look.sky.haze_height = 1800;
    list[1].look.sky.haze_forward = .82;
    list[1].look.crest_haze = 1.6;
    // C: lower, warmer light, the sun just over the brink, the river darker gold.
    list[2].name = "C";
    list[2].label = "late gold";
    list[2].look.sky.sun_elevation = 43 * cf::pi / 180;
    list[2].look.sky.sun_azimuth = 1.5 * cf::pi / 180;
    list[2].look.sky.haze = 4.0;
    list[2].look.sky.sun_tint = 2.2;
    list[2].look.water_amber = 1.4;
    list[2].look.glints = 1.3;

    // Each candidate whole; its crest close, as through binoculars from the walkway;
    // and its sky alone, from the horizon up past the sun.
    cf::Camera crest;
    crest.fov = 9 * cf::pi / 180;
    crest.bend = 0;
    crest.yaw = 3 * cf::pi / 180;
    crest.pitch = 17.5 * cf::pi / 180;
    cf::Camera sky;
    sky.fov = 62 * cf::pi / 180;
    sky.bend = 0;
    sky.pitch = 36 * cf::pi / 180;
    // A seventh argument asks for the first candidate's whole view alone (quick looks).
    const bool quick = argc > 7;
    for (Candidate& candidate : list) {
        if (quick && &candidate != &list[0])
            break;
        for (int view = 0; view < (quick ? 1 : 3); ++view) {
            const int w = view == 1 ? 900 : width;
            const int h = view == 0 ? height : view == 1 ? 520 : 620;
            cf::LinearImage image;
            if (view == 0)
                cf::render_still(candidate.look, candidate.camera, w, h, 0, std::min(rows, h), time, samples, image);
            else if (view == 1)
                cf::render_still(candidate.look, crest, w, h, 0, h, time, 3, image);
            else
                cf::render_sky(candidate.look, sky, w, h, image);
            report(candidate.name, image);
            cf::add_bloom(image, 1.0 / candidate.look.exposure);
            std::vector<std::uint8_t> bgra;
            cf::tone_map(image, candidate.look.exposure, bgra);
            const char* suffix = view == 0 ? "" : view == 1 ? "-crest" : "-sky";
            const std::string path = directory + "/" + candidate.name + suffix + ".png";
            if (!kit::write_png(path, image.width, image.height, bgra)) {
                std::fprintf(stderr, "could not write %s\n", path.c_str());
                return 1;
            }
            std::printf("%s (%s) -> %s\n", candidate.name, candidate.label, path.c_str());
        }
    }
    return 0;
}
