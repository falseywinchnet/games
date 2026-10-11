#include "borrowed_goals.hpp"
#include <algorithm>
#include <stdexcept>
namespace ct::research {
std::vector<int> borrowable_goals(const NestedOrchard& o) {
    const auto& c=o.certificate;const int n=static_cast<int>(c.shape.parent.size());
    std::vector<int> out;
    for(int v=0;v<n;++v)
        if((v<o.components||v>c.shape.start)&&!(v==0&&c.root_pit))out.push_back(v);
    return out;
}
BorrowedGoals borrow_goals(NestedOrchard o,const std::vector<int>& pumpkins) {
    auto eligible=borrowable_goals(o);std::vector<bool> selected(o.certificate.shape.parent.size());
    for(int v:pumpkins) {
        if(std::find(eligible.begin(),eligible.end(),v)==eligible.end()||selected[v])
            throw std::invalid_argument("borrowed goal lacks a first-entry certificate");
        selected[v]=true;
    }
    auto& c=o.certificate;std::vector<bool> seen(selected.size());std::vector<PushIntent> plan;
    plan.reserve(c.plan.size()+pumpkins.size());
    for(auto p:c.plan) {
        if(selected[p.pumpkin]&&!seen[p.pumpkin]) {
            if(p.direction!=kRight)throw std::logic_error("borrowed goal does not begin with temporary entry");
            plan.push_back(p);seen[p.pumpkin]=true;
        }
        plan.push_back(p);
    }
    for(int v:pumpkins)if(!seen[v])throw std::logic_error("borrowed goal missing from certificate");
    c.plan=std::move(plan);c.exact_pushes+=static_cast<int>(pumpkins.size());
    return {std::move(o),pumpkins,static_cast<int>(pumpkins.size())};
}
std::vector<int> place_borrowed_goals(PortGraph& graph,const BorrowedGoals& c,OrchardLayout layout) {
    auto centers=graph.pumpkins;const int n=static_cast<int>(centers.size());
    if(n!=static_cast<int>(c.orchard.certificate.shape.parent.size()))throw std::invalid_argument("borrowed graph shape mismatch");
    for(int v:c.initial_goals) {
        int goal=graph.sites[centers[v]].port[orchard_direction(kLeft,v,n,layout)];
        if(goal<0||!graph.sites[goal].goal)throw std::logic_error("borrowed goal geometry mismatch");
        graph.pumpkins[v]=goal;
    }
    return centers;
}
}
