#include "collection.hpp"
#include "storage.hpp"
#include "test_paths.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <set>

int main() {
    const std::filesystem::path scratch=games_test::scratch_directory("playsuite-catalog-");
    games_test::isolate_saves(scratch);
    games::Cabinet cabinet;
    cabinet.music=false; cabinet.sound=false; cabinet.reduced=true;
    assert(games::save_cabinet(games::cabinet_path(),cabinet));
    const std::filesystem::path state=scratch/"playsuite-entry.txt";
    {
        // A future/temporarily absent module must not strand the shell. Retain
        // its progress independently of the installed catalog and 32-bit masks.
        std::ofstream saved(state);
        saved<<"v2 2147483646 1 2147483646 1000000\n";
    }
    {
        std::shared_ptr<games::Collection> collection=gui_forms::make_control<games::Collection>(gui_forms::StableId("catalog.collection"),true);
        gui_forms::Window window(collection,{600,420});
        window.set_active(true); window.perform_layout();
        assert((*collection).shelf_open());
        assert(games::valid_entry((*collection).active()));
        assert((*collection).children().size()==5);
        std::set<int> ids;
        for(games::Entry entry:games::entries) {
            assert(ids.insert(static_cast<int>(entry)).second);
            const games::GameDescriptor& descriptor=games::game_descriptor(entry);
            assert(games::find_entry(descriptor.id)==entry);
            assert(descriptor.help && descriptor.help[0]);
            (*collection).open_entry(entry);
            window.perform_layout();
            assert((*collection).active()==entry && !(*collection).shelf_open());
            for(const games::HelpTopic& topic:descriptor.help_topics) {
                (*collection).dispatch_command(topic.id);
                assert((*collection).help_open());
                (*collection).close_help();
            }
            (*collection).show_help();
            assert((*collection).help_open());
            (*collection).show_shelf();
            assert((*collection).shelf_open() && !(*collection).help_open());
        }
        const games::Entry previous=(*collection).active();
        (*collection).open_entry(static_cast<games::Entry>(-17));
        assert((*collection).active()==previous);
    }
    {
        std::ifstream saved(state);
        std::string version; int active=-1,playing=-1;
        saved>>version>>active>>playing;
        assert(version=="v2" && playing==0 && active==static_cast<int>(games::entries.back()));
        std::set<int> opened; int id=-1;
        while(saved>>id) opened.insert(id);
        assert(opened.contains(2147483646) && opened.contains(1000000));
        for(games::Entry entry:games::entries) assert(opened.contains(static_cast<int>(entry)));
    }
    {
        std::shared_ptr<games::Collection> resumed=gui_forms::make_control<games::Collection>(gui_forms::StableId("catalog.resumed"),true);
        assert((*resumed).shelf_open() && (*resumed).active()==games::entries.back());
    }
    {
        // Upgrade an original bitmask save without changing persistent IDs.
        std::ofstream saved(state);
        saved<<static_cast<int>(games::entries.front())<<" 0 2147483649\n";
    }
    {
        std::shared_ptr<games::Collection> legacy=gui_forms::make_control<games::Collection>(gui_forms::StableId("catalog.legacy"),true);
        (*legacy).show_shelf();
    }
    {
        std::ifstream saved(state); std::string version; int active,playing,id;
        saved>>version>>active>>playing;
        std::set<int> opened;
        while(saved>>id) opened.insert(id);
        assert(version=="v2" && opened.contains(0) && opened.contains(31));
    }
    std::filesystem::remove_all(scratch);
    std::cout<<"Catalog factories, lazy creation, help, absent modules and stable-ID save migration pass.\n";
}
