#include "satsolv/congruence.hpp"
#include <algorithm>
#include <cstddef>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace satsolv {
namespace {
int var(int x) noexcept { return x < 0 ? -x : x; }
int code(int x) noexcept { return 2*(var(x)-1) + (x < 0 ? 1 : 0); }
int literal(int x) noexcept { return (x & 1) != 0 ? -(x/2+1) : x/2+1; }
bool less_lit(int a, int b) noexcept { return var(a) != var(b) ? var(a) < var(b) : a < b; }
bool normalize(std::vector<int>& c) {
    std::sort(c.begin(), c.end(), less_lit);
    c.erase(std::unique(c.begin(), c.end()), c.end());
    for (std::size_t i=1; i<c.size(); ++i) if (c[i-1] == -c[i]) return false;
    return true;
}
std::uint64_t pair_key(int a, int b) noexcept {
    auto x = static_cast<std::uint32_t>(code(a)), y = static_cast<std::uint32_t>(code(b));
    if (x > y) std::swap(x,y);
    return (static_cast<std::uint64_t>(x)<<32U) | y;
}
struct Work {
    std::uint64_t left;
    const std::function<bool()>& interrupted;
    std::uint64_t used = 0;
    bool take() {
        if (left == 0) return false;
        --left; ++used;
        if ((used & 1023U) == 0U && interrupted()) { left=0; return false; }
        return true;
    }
};
class SignedUnion {
    std::vector<int> parent_;
    int find_code(int x) {
        int root=x;
        while (parent_[static_cast<std::size_t>(root)] != root) root=parent_[static_cast<std::size_t>(root)];
        while (x != root) { const int next=parent_[static_cast<std::size_t>(x)]; parent_[static_cast<std::size_t>(x)]=root; x=next; }
        return root;
    }
    void unite_codes(int a, int b) {
        a=find_code(a); b=find_code(b);
        if (a != b) parent_[static_cast<std::size_t>(std::max(a,b))]=std::min(a,b);
    }
public:
    explicit SignedUnion(int n) : parent_(static_cast<std::size_t>(2*n)) { std::iota(parent_.begin(),parent_.end(),0); }
    int find(int x) { return literal(find_code(code(x))); }
    bool join(int a, int b) {
        if (find(a) == find(-b)) return false;
        unite_codes(code(a),code(b)); unite_codes(code(-a),code(-b)); return true;
    }
};
// Iterative Kosaraju. An exhausted graph traversal produces no SCC conclusion.
bool components(const Formula& f, Work& work, std::vector<int>& component) {
    const auto count=static_cast<std::size_t>(2*f.variables);
    std::vector<std::vector<int>> forward(count), reverse(count);
    for (const auto& c : f.clauses) {
        if (!work.take()) return false;
        if (c.size() != 2) continue;
        const int a=code(c[0]), b=code(c[1]);
        forward[static_cast<std::size_t>(a^1)].push_back(b); reverse[static_cast<std::size_t>(b)].push_back(a^1);
        forward[static_cast<std::size_t>(b^1)].push_back(a); reverse[static_cast<std::size_t>(a)].push_back(b^1);
    }
    std::vector<std::uint8_t> seen(count,0); std::vector<int> order; order.reserve(count);
    std::vector<std::pair<int,std::size_t>> stack;
    for (std::size_t start=0; start<count; ++start) {
        if (!work.take()) return false;
        if (seen[start] != 0) continue;
        seen[start]=1; stack.emplace_back(static_cast<int>(start),0);
        while (!stack.empty()) {
            auto& [node, next]=stack.back();
            const auto& edges=forward[static_cast<std::size_t>(node)];
            if (next == edges.size()) { order.push_back(node); stack.pop_back(); continue; }
            if (!work.take()) return false;
            const int target=edges[next++];
            if (seen[static_cast<std::size_t>(target)] == 0) { seen[static_cast<std::size_t>(target)]=1; stack.emplace_back(target,0); }
        }
    }
    component.assign(count,-1); std::vector<int> pending; int id=0;
    for (auto it=order.rbegin(); it!=order.rend(); ++it) {
        if (!work.take()) return false;
        if (component[static_cast<std::size_t>(*it)] >= 0) continue;
        component[static_cast<std::size_t>(*it)]=id; pending.push_back(*it);
        while (!pending.empty()) {
            const int node=pending.back(); pending.pop_back();
            for (int target : reverse[static_cast<std::size_t>(node)]) {
                if (!work.take()) return false;
                if (component[static_cast<std::size_t>(target)] >= 0) continue;
                component[static_cast<std::size_t>(target)]=id; pending.push_back(target);
            }
        }
        ++id;
    }
    return true;
}
}

CongruenceResult simplify_congruence(const Formula& input, std::uint64_t budget,
    const std::function<void(const std::vector<int>&)>& emit,
    const std::function<bool()>& interrupted) {
    CongruenceResult result;
    std::uint64_t literals=0;
    for (const auto& c : input.clauses) literals += c.size();
    if (budget == 0 || input.variables > 200000 || literals > 2000000 || interrupted()) return result;
    result.formula.variables=input.variables;
    for (auto c : input.clauses) if (normalize(c)) result.formula.clauses.push_back(std::move(c));
    SignedUnion equiv(input.variables); Work work{budget,interrupted};
    std::set<int> units;
    bool contradiction=false;
    auto equivalence = [&](int a, int b) {
        if (equiv.find(a) == equiv.find(b)) return false;
        // These binary lemmas are RUP either from a complete SCC path or a
        // recognized pair of complete AND encodings with equivalent inputs.
        for (auto c : {std::vector<int>{-a,b},std::vector<int>{a,-b}}) if (normalize(c)) emit(c);
        ++result.equivalences;
        if (!equiv.join(a,b)) {
            // A signed-cycle contradiction is not necessarily unit-conflicting
            // without a seed. First prove a by failed-literal propagation, then
            // the empty clause follows by ordinary unit propagation.
            emit({a}); emit({}); contradiction=true;
        }
        return true;
    };
    auto rewrite = [&]() {
        std::vector<std::vector<int>> rewritten;
        rewritten.reserve(result.formula.clauses.size());
        for (const auto& c : result.formula.clauses) {
            std::vector<int> mapped; mapped.reserve(c.size());
            for (int lit : c) mapped.push_back(equiv.find(lit));
            if (!normalize(mapped)) continue;
            if (mapped != c) emit(mapped);
            rewritten.push_back(std::move(mapped));
        }
        std::sort(rewritten.begin(),rewritten.end());
        rewritten.erase(std::unique(rewritten.begin(),rewritten.end()),rewritten.end());
        result.formula.clauses.swap(rewritten);
    };
    for (unsigned round=0; round<32 && work.left>0 && !interrupted(); ++round) {
        ++result.rounds; bool changed=false;
        std::vector<int> component;
        if (!components(result.formula,work,component)) break;
        for (int v=1; v<=input.variables; ++v) {
            if (component[static_cast<std::size_t>(code(v))] == component[static_cast<std::size_t>(code(-v))]) {
                // Under -v, the SCC implies v; adding v is RUP. With v present,
                // the reverse path implies -v, so the empty clause is RUP.
                emit({v}); emit({}); contradiction=true; break;
            }
        }
        if (contradiction) break;
        std::map<int,int> representative;
        for (std::size_t i=0; i<component.size(); ++i) {
            const int lit=literal(static_cast<int>(i));
            const auto [it, inserted]=representative.emplace(component[i],lit);
            if (!inserted) changed=equivalence(lit,it->second) || changed;
        }
        if (contradiction) break;
        // Index complete binary constraints. A longer clause alone is never
        // treated as a gate, preventing false functional-equivalence claims.
        std::unordered_set<std::uint64_t> binary;
        for (const auto& c : result.formula.clauses) if (c.size()==2) binary.insert(pair_key(c[0],c[1]));
        std::map<std::vector<int>,int> gates;
        std::vector<std::vector<int>> new_units;
        for (const auto& c : result.formula.clauses) {
            if (!work.take()) break;
            if (c.size()<3 || c.size()>9) continue;
            for (int output : c) {
                std::vector<int> inputs; bool complete=true;
                for (int lit : c) if (lit != output) {
                    if (!work.take()) { complete=false; break; }
                    const int input_lit=-lit;
                    if (!binary.contains(pair_key(-output,input_lit))) { complete=false; break; }
                    inputs.push_back(equiv.find(input_lit));
                }
                if (!complete) continue;
                ++result.and_gates;
                if (!normalize(inputs)) {
                    if (units.insert(-output).second) { emit({-output}); new_units.push_back({-output}); changed=true; }
                    continue;
                }
                if (inputs.size()==1) changed=equivalence(output,inputs[0]) || changed;
                else {
                    const auto [it, inserted]=gates.emplace(inputs,output);
                    if (!inserted) changed=equivalence(output,it->second) || changed;
                }
                if (contradiction) break;
            }
            if (contradiction) break;
        }
        if (contradiction) break;
        for (auto& c : new_units) result.formula.clauses.push_back(std::move(c));
        if (!changed) break;
        result.applied=true; rewrite();
    }
    if (contradiction) { result.formula.clauses={{}}; result.applied=true; }
    // Any completed equivalence remains sound when a later budget is exhausted.
    if (result.equivalences != 0 && !contradiction) { result.applied=true; rewrite(); }
    if (result.applied) {
        result.representatives.resize(static_cast<std::size_t>(input.variables+1));
        for (int v=1; v<=input.variables; ++v) {
            result.representatives[static_cast<std::size_t>(v)]=equiv.find(v);
            if (equiv.find(-v) != -equiv.find(v)) throw std::logic_error("inconsistent signed equivalence mapping");
        }
    }
    result.work=work.used;
    return result;
}

void restore_equivalent_model(std::vector<int>& model,const std::vector<int>& reps) {
    if (reps.empty()) return;
    if (reps.size()!=model.size()+1) throw std::logic_error("equivalence model shape mismatch");
    const auto old=model;
    for (std::size_t v=1; v<reps.size(); ++v) {
        const int rep=reps[v];
        const bool value=old[static_cast<std::size_t>(var(rep)-1)] > 0;
        model[v-1]=(value == (rep>0)) ? static_cast<int>(v) : -static_cast<int>(v);
    }
}
} // namespace satsolv
