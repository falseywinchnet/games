#pragma once
#include "level.hpp"
#include <array>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ct::research {
// An orthogonal movement graph. Coordinates belong to the compiler, not sites.
// Pushes use two consecutive ports of the same direction: support -> box -> end.
struct Site {
    std::array<int,4> port{{-1,-1,-1,-1}};
    bool goal=false, pit=false;
    int gate=0;
};
struct PortGraph {
    std::vector<Site> sites;
    // Sparse trailing diagnostic storage: unnamed road sites need no string
    // object in the movement record. Names never affect graph semantics.
    std::vector<std::string> names;
    const std::string& name(int site) const;
    int find_site(std::string_view name) const;
    std::vector<int> pumpkins, raccoons; // squirrels use the legacy level field name
    // Ordered pairs (bear site, pumpkin site) encode the game's guarding rule.
    // Only pairs for pumpkin sites adjacent to a sentry are needed.
    std::vector<std::pair<int,int>> guards;
    int player=-1;
    int add_unnamed(Site site={}) {sites.push_back(site);return static_cast<int>(sites.size())-1;}
    int add(std::string name);
    void join(int from,int to,int direction);
    int extend(int from,int direction,std::string name);
    bool edge(int from,int direction) const;
};

// Independent operational semantics, used before there is a Level or Board.
// IDs track trajectories and certificates; all pumpkins obey the same rules.
struct GraphState {
    const PortGraph* graph;
    int player;
    std::vector<int> pumpkins,occupied;
    std::vector<unsigned char> filled, departed;
    explicit GraphState(const PortGraph&);
    int box_at(int site) const;
    bool sentry_at(int site) const;
    bool walkable(int site) const;
    bool move(int direction,bool* pushed=nullptr);
    bool solved() const;
};

struct Embedding {
    Level level;
    std::vector<int> cell_for_site;
};
// Throws on graph/geometry mismatch, including incidental adjacency. This is a
// proof checker for a constructive grammar, never a seed-rejection interface.
Embedding embed(const PortGraph&,int quarter_turns=0,bool reflect=false);

// Fixed push plan -> exact movement certificate. Walking BFS does not choose
// pushes. Each chosen walk is replayed through turn effects. A walking path
// causing theft fails the certificate; this planner does not search for a safer
// alternative. Constructive families must establish that their walks are safe.
struct PushIntent { int pumpkin, direction; };
// Orient before routing, so world-direction sentry priority is evaluated anew.
void orient(PortGraph&,std::vector<PushIntent>&,int quarter_turns,bool reflect);
std::string route_plan(const PortGraph&,const std::vector<PushIntent>&);
// A consequence score can explicitly prescribe effect-free walks or spend a
// walk to trigger one theft. Explicit plain walks use pumpkin=raccoon=-1.
// Push actions in this interface must not cause incidental theft. A theft walk
// must remove exactly pumpkin and depart exactly raccoon; all other resources
// remain unchanged. It is executed at the current bear site, never searched.
struct ActionIntent {
    enum class Kind { push, theft_walk, walk };
    int pumpkin, direction;
    Kind kind=Kind::push;
    int raccoon=-1; // squirrel index in PortGraph::raccoons
};
void orient_actions(PortGraph&,std::vector<ActionIntent>&,int quarter_turns,bool reflect);
std::string route_actions(const PortGraph&,const std::vector<ActionIntent>&);
std::string transform_trace(const std::string&,int quarter_turns,bool reflect);
std::string graph_dot(const PortGraph&);
}
