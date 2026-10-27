#pragma once
#include <vector>
#include "port_graph.hpp"
namespace ct::research {
struct ForestShape {std::vector<int> parent;int start=0;};
// Insert immediately before v in the temporary cycle. Both new decisions use
// the old goal predecessor: the old temporary connection now requires both
// pumpkins in their temporary positions. v must have a non-self predecessor.
ForestShape split_decision(const ForestShape&,int v);
// Insert a new self-root before v (v==n means before cyclic vertex 0). It adds a
// final component and subdivides a temporary connection, without adding a
// connection between previously separate old junctions. Never move the start
// onto the inserted vertex. These functions operate before geometry exists.
ForestShape insert_component(const ForestShape&,int v);
struct ForestCertificate {
    ForestShape shape;
    std::vector<PushIntent> plan; // Local turnout directions, before geometry.
    int exact_pushes=0;
    bool root_pit=false;
};
// Typed refinements: eligibility inspects only the input graph/certificate.
// No solver or enumeration of candidate output graphs is involved.
bool can_split_certificate(const ForestCertificate&,int v);
ForestCertificate split_certificate(const ForestCertificate&,int v);
// Split the cyclic return into root zero by appending an ordinary root child.
// This rule requires a classic root; the pit's permanent edge needs a different
// lifetime argument and is not silently treated as a goal endpoint.
bool can_split_root_return(const ForestCertificate&);
ForestCertificate split_root_return(const ForestCertificate&);
bool can_insert_terminal_component(const ForestCertificate&);
ForestCertificate insert_terminal_component(const ForestCertificate&);
// Divide the starting leaf's output route between two pumpkins, retaining the
// start on the first junction. The second pumpkin must open the only initial
// exit temporarily. This adds a coupled commitment, with exact extra cost 3.
bool can_split_start_leaf(const ForestCertificate&);
ForestCertificate split_start_leaf(const ForestCertificate&);
}
