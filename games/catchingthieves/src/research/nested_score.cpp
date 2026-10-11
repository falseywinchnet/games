#include "nested_score.hpp"
#include "tiers.hpp"
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace ct::research {
NestedScore compose_nested_score(int difficulty,std::uint64_t seed) {
    if(difficulty<kEasy||difficulty>kHard)throw std::invalid_argument("invalid score tier");
    std::uint64_t state=seed;
    auto random=[&]{state+=0x9e3779b97f4a7c15ULL;auto z=state;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);};
    int width=difficulty;std::vector<int> count;
    if(difficulty==kEasy) {
        int components=1+static_cast<int>(random()%2);count.assign(components,1);
        int extra=components==1?2+static_cast<int>(random()%3):static_cast<int>(random()%2);
        for(int i=0;i<extra;++i)++count[random()%count.size()];
    } else if(difficulty==kMedium) {
        int components=2+static_cast<int>(random()%2);count.assign(components,1);
        // The last outer child and a child of component one straddle the
        // initial cut. Extra children belong to the interior components.
        int extra=static_cast<int>(random()%(std::min(2,8-2*components)+1));
        for(int i=0;i<extra;++i)++count[1+random()%(components-1)];
    } else {
        width=3+static_cast<int>(random()%3);
        int components=3+static_cast<int>(random()%(std::min(width,4)-2));
        count.assign(components,1);
        for(int i=components;i<width;++i)++count[random()%count.size()];
        int extra=static_cast<int>(random()%(std::min(2,10-components-width)+1));
        count.back()+=extra; // Goal-first prefix; all components span the cut.
    }
    const bool pit=difficulty>=kMedium;
    auto orchard=nested_orchard(count,random(),pit,width);
    std::vector<int> selected;
    if(difficulty>=kMedium) {
        // Different goal components lend their occupied goals to the return
        // route. Every selected endpoint has a first-entry lower bound.
        std::vector<std::vector<int>> candidates(count.size());
        for(int v:borrowable_goals(orchard))candidates[orchard.certificate.shape.parent[v]].push_back(v);
        std::vector<int> components;for(int p=0;p<static_cast<int>(count.size());++p)if(!candidates[p].empty())components.push_back(p);
        int needed=difficulty==kMedium?1:2+static_cast<int>(random()%2);
        if(static_cast<int>(components.size())<needed)throw std::logic_error("missing borrowed-goal components");
        for(std::size_t i=components.size();i>1;--i)std::swap(components[i-1],components[random()%i]);
        for(int i=0;i<needed;++i){auto& choices=candidates[components[i]];selected.push_back(choices[random()%choices.size()]);}
    }
    NestedScore score;score.certificate=borrow_goals(std::move(orchard),selected);
    score.difficulty=difficulty;score.seed=seed;score.raccoon=difficulty==kHard;
    score.quarter_turns=static_cast<int>(random()%4);score.reflect=random()%2;
    score.garden_seed=random();return score;
}
}
namespace ct::research {
std::string score_graph_dot(const NestedScore& score) {
    const auto& borrowed=score.certificate;const auto& c=borrowed.orchard.certificate;
    const int n=static_cast<int>(c.shape.parent.size());std::ostringstream out;
    out<<"digraph score {\n  rankdir=LR;\n  label=\"Conditional action score: tier "<<score.difficulty<<", seed "<<score.seed
       <<"\\nminimum pushes "<<c.exact_pushes<<", unfinished width "<<borrowed.orchard.minimum_width
       <<", goal departures "<<borrowed.minimum_goal_departures<<"\";\n  labelloc=t;\n";
    for(int v=0;v<n;++v)out<<"  j"<<v<<" [shape=ellipse,label=\"Access "<<v<<(v==c.shape.start?" (start)":"")<<"\"];\n";
    if(c.root_pit)out<<"  highway [shape=ellipse,label=\"Return highway\\ninitially beyond open pit\"];\n";
    for(int v=0;v<n;++v) {
        const bool initial=std::find(borrowed.initial_goals.begin(),borrowed.initial_goals.end(),v)!=borrowed.initial_goals.end();
        if(v==0&&c.root_pit) {
            out<<"  t0 [shape=box,label=\"Pumpkin 0: center -> parked\\n1 push; requires center\"];\n"
               <<"  fill [shape=box,label=\"Pumpkin 0: parked -> consumed\\n1 push; fills pit\"];\n"
               <<"  j"<<n-1<<" -> t0 [label=\"reach support\"]; t0 -> j0;\n"
               <<"  j0 -> fill [label=\"reach support\"]; fill -> highway;\n";
            continue;
        }
        out<<"  t"<<v<<" [shape=box,label=\"Pumpkin "<<v<<": open temporary input\\ncenter/goal -> parked (1/2 pushes)"
           <<(initial?"\\ninitially on goal; departure required":"")<<"\"];\n"
           <<"  g"<<v<<" [shape=box,label=\"Pumpkin "<<v<<": finish or restore goal input\\ncenter/parked -> goal (1/2 pushes)\"];\n"
           <<"  j"<<(v+n-1)%n<<" -> t"<<v<<" [label=\"reach support\"]; t"<<v<<" -> j"<<v<<";\n";
        if(c.root_pit&&c.shape.parent[v]==0)out<<"  highway";else out<<"  j"<<c.shape.parent[v];
        out<<" -> g"<<v<<" [label=\"reach support\"]; g"<<v<<" -> j"<<v<<";\n";
    }
    out<<"  note [shape=note,label=\"Opening a temporary input closes its goal input.\\nRestoring a goal closes its temporary input.\\nA filled pit keeps its connection.\\nThe final goal input is one-way; walks must guard exposed pumpkins.\\nEdges are conditional requirements, not simultaneous roads.\\nPumpkin numbers track identical actors.\"];\n}\n";
    return out.str();
}
}
