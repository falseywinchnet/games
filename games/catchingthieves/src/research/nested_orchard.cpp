#include "nested_orchard.hpp"
#include <algorithm>
#include <numeric>
#include <stdexcept>
namespace ct::research {
NestedOrchard nested_orchard(const std::vector<int>& count,std::uint64_t seed,bool pit,int unfinished) {
    int k=static_cast<int>(count.size());
    if(k<1||k>5||std::any_of(count.begin(),count.end(),[](int n){return n<1||n>9;}))throw std::invalid_argument("invalid nested component multiplicities");
    int returns=std::accumulate(count.begin(),count.end(),0),n=k+returns;
    if(n>10)throw std::invalid_argument("nested orchard needs at most ten pumpkins");
    if(unfinished==-1)unfinished=returns;
    if(unfinished<1||unfinished>returns)throw std::invalid_argument("invalid unfinished-pumpkin width");
    auto random=[&]{seed+=0x9e3779b97f4a7c15ULL;auto z=seed;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);};
    NestedOrchard out;out.components=k;out.minimum_width=unfinished;
    auto& c=out.certificate;c.root_pit=pit;c.shape.start=n-unfinished;
    c.exact_pushes=3*k+returns+2*(unfinished-1)-(pit?1:0);
    for(int p=0;p<k;++p)c.shape.parent.push_back(p);
    std::vector<std::vector<int>> children(k);
    for(int p=k-1;p>=0;--p)for(int i=0;i<count[p];++i) {
        children[p].push_back(static_cast<int>(c.shape.parent.size()));c.shape.parent.push_back(p);
    }
    // Each goal input is in an as-yet unvisited root. The only initial escape
    // visits the remaining children in cycle order, temporarily parking each.
    for(int v=c.shape.start+1;v<n;++v)c.plan.push_back({v,kRight});
    for(int p=0;p<k;++p) {
        c.plan.push_back({p,kRight});
        if(p==0&&pit)c.plan.push_back({p,kRight});
        else {c.plan.push_back({p,kLeft});c.plan.push_back({p,kLeft});}
        // This component's predecessor has no unfinished obligations when its
        // root closes. Its goal edges keep every completed child accessible.
        auto order=children[p];
        for(std::size_t i=order.size();i>1;--i)std::swap(order[i-1],order[random()%i]);
        for(int v:order) {
            c.plan.push_back({v,kLeft});if(v>c.shape.start)c.plan.push_back({v,kLeft});
        }
    }
    if(static_cast<int>(c.plan.size())!=c.exact_pushes)throw std::logic_error("nested certificate cost invariant");
    return out;
}
}
