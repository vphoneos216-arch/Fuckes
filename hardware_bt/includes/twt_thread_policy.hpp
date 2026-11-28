#pragma once
#include <algorithm>
#include <cstddef>
#include <vector>

namespace twt_policy {
struct Candidate {int tid=0;int priority=0;};
inline size_t thread_budget(int slots,unsigned points){
    if(!points)return 0;
    const size_t handles=static_cast<size_t>(slots>0?std::min(slots,8):4);
    return handles/points;
}
// Verified callers stay resident; spare capacity samples other live TIDs fairly.
inline std::vector<int> select(std::vector<Candidate> candidates,const std::vector<int>& pinned,
                               const std::vector<int>& current,size_t budget,size_t& cursor,bool rotate){
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.priority!=b.priority?a.priority>b.priority:a.tid<b.tid;});
    std::vector<int> result;result.reserve(budget);
    auto add=[&](int tid){
        if(result.size()<budget && std::any_of(candidates.begin(),candidates.end(),[&](const auto& p){return p.tid==tid;}) &&
           std::find(result.begin(),result.end(),tid)==result.end())result.push_back(tid);
    };
    for(int tid:pinned)add(tid);
    if(!rotate)for(int tid:current)add(tid);
    size_t examined=0;
    while(result.size()<budget&&examined<candidates.size()){
        const size_t index=(cursor+examined)%candidates.size();add(candidates[index].tid);++examined;
    }
    if(!candidates.empty()&&(rotate||current.empty()))cursor=(cursor+examined)%candidates.size();
    return result;
}
}
