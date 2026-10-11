#include "garden_space.hpp"
#include <algorithm>
#include <array>
#include <stdexcept>
namespace ct::research {
namespace {
GardenSpace expand_turnout_impl(Level original,std::uint64_t seed,bool vertical,const std::vector<int>& centers) {
    GardenSpace out{std::move(original),{}};auto& lv=out.level;
    if(lv.w<3||lv.h<3||lv.w>40||lv.h>40||lv.tiles.size()!=static_cast<std::size_t>(lv.w*lv.h))
        throw std::invalid_argument("invalid turnout expansion dimensions");
    out.region.assign(lv.tiles.size(),-1);
    const int size=lv.w*lv.h;const std::array<int,4> delta{-lv.w,1,lv.w,-1};
    // Every valid garden fits in these local arrays. They are per-call, so
    // worker generation needs no shared scratch storage or synchronization.
    std::array<unsigned char,1600> rigid{},floor{};
    std::array<std::uint64_t,42> row_floor{}; // one empty sentinel row on each side
    std::array<int,1600> floors,queue,starts,lengths;
    int floor_count=0,queued=0,region_count=0;
    for(int y=0,c=0;y<lv.h;++y)for(int x=0;x<lv.w;++x,++c)if(lv.tiles[c]==Tile::floor) {
        if(y==0||y+1==lv.h||x==0||x+1==lv.w)
            throw std::invalid_argument("turnout floor touches board boundary");
        floor[c]=1;floors[floor_count++]=c;row_floor[y+1]|=std::uint64_t{1}<<x;
    }
    auto freeze=[&](int c){
        if(c<lv.w||c>=size-lv.w||c%lv.w==0||c%lv.w==lv.w-1)
            throw std::invalid_argument("turnout support halo exceeds board interior");
        rigid[c]=1;for(int d:delta)rigid[c+d]=1;
    };
    for(int box:centers.empty()?lv.boxes:centers)for(int along=-2;along<=2;++along) {
        int c=box+along*(vertical?lv.w:1);freeze(c);
        if(!lv.raccoons.empty())for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)rigid[c+y*lv.w+x]=1;
    }
    for(int c=0;c<size;++c)if(lv.gate_at(c)||lv.pit_at(c))freeze(c);
    for(int c:lv.raccoons)freeze(c);
    // Append each component to one arena, retaining the original BFS order.
    // Component IDs, PRNG calls and proposal order stay byte-for-byte stable.
    for(int f=0;f<floor_count;++f)if(int c=floors[f];!rigid[c]&&out.region[c]<0) {
        int r=region_count++,begin=queued;starts[r]=begin;queue[queued++]=c;out.region[c]=r;
        for(int h=begin;h<queued;++h)for(int d:delta) {
            int to=queue[h]+d;if(floor[to]&&!rigid[to]&&out.region[to]<0){out.region[to]=r;queue[queued++]=to;}
        }
        lengths[r]=queued-begin;
        if(!lv.raccoons.empty()&&lengths[r]==1){out.region[c]=-1;rigid[c]=1;lengths[r]=0;}
    }
    auto random=[&]{seed+=0x9e3779b97f4a7c15ULL;auto z=seed;z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;z=(z^(z>>27))*0x94d049bb133111ebULL;return z^(z>>31);};
    constexpr auto offsets=[] {
        std::array<std::array<int,2>,28> result{};int at=0;
        for(int distance=1;distance<=4;++distance)for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x)
            if((x<0?-x:x)+(y<0?-y:y)==distance&&x*x+y*y<=9)result[at++]={x,y};
        return result;
    }();
    // Each disc is processed from its center outward. The bounded local pass
    // never needs global shuffling, per-cell proposal lists or repeated scans.
    for(int r=0;r<region_count;++r) {
        int length=lengths[r];if(!length)continue;
        for(int sample=0;sample<length;sample+=24) {
            int center=queue[starts[r]+random()%length],cx=center%lv.w,cy=center/lv.w,radius=2+random()%2;
            int turn=random()%4;
            for(auto offset:offsets) {
                int x=offset[0],y=offset[1];if(x*x+y*y>radius*radius)continue;
                int dx=x,dy=y;
                if(turn==1){dx=-y;dy=x;}else if(turn==2){dx=-x;dy=-y;}else if(turn==3){dx=y;dy=-x;}
                int px=cx+dx,py=cy+dy;if(px<1||px+1>=lv.w||py<1||py+1>=lv.h)continue;
                int c=py*lv.w+px;if(floor[c]||rigid[c])continue;
                bool attached=false,legal=true;
                for(int d:delta)if(floor[c+d]){attached=true;if(out.region[c+d]!=r){legal=false;break;}}
                if(attached&&legal){lv.tiles[c]=Tile::floor;floor[c]=1;row_floor[py+1]|=std::uint64_t{1}<<px;out.region[c]=r;++out.added_floor;}
            }
        }
    }
    // Chebyshev dilation of the final floor: three row unions and two shifts
    // replace nine scatter writes per floor cell. Width is bounded by forty.
    for(int y=0,c=0;y<lv.h;++y) {
        auto neighbors=row_floor[y]|row_floor[y+1]|row_floor[y+2];
        auto hedge=neighbors|(neighbors<<1)|(neighbors>>1);
        for(int x=0;x<lv.w;++x,++c)if(!floor[c])lv.tiles[c]=(hedge&(std::uint64_t{1}<<x))?Tile::wall:Tile::outside;
    }
    return out;
}
}
}

namespace ct::research {
GardenSpace expand_turnout_garden(const Level& level,std::uint64_t seed,bool vertical,const std::vector<int>& centers) {
    return expand_turnout_impl(level,seed,vertical,centers);
}
GardenSpace expand_turnout_garden(Level&& level,std::uint64_t seed,bool vertical,const std::vector<int>& centers) {
    return expand_turnout_impl(std::move(level),seed,vertical,centers);
}
}
