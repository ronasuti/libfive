#pragma once

#include <vector>

#include <boost/bimap.hpp>

#include "ao/eval/clause.hpp"
#include "ao/eval/interval.hpp"
#include "ao/tree/tree.hpp"

namespace Kernel {

class Tape
{
public:
    Tape(const Tree root);

    /*  Returned by evaluator types when pushing  */
    enum Keep { KEEP_BOTH, KEEP_A, KEEP_B };

    /*  Different kind of tape pushes  */
    enum Type { UNKNOWN, INTERVAL, SPECIALIZED, FEATURE };

    /*
     *  Pops one tape off the stack, re-enabling disabled nodes
     */
    void pop();

    /*
     *  Returns the fraction active / total nodes
     *  (to check how well disabling is working)
     */
    double utilization() const;

protected:

    struct Subtape
    {
        /*  The tape itself, as a vector of clauses  */
        std::vector<Clause> t;

        /*  Root clause of the tape  */
        Clause::Id i;

        /*  These bounds are only valid if type == INTERVAL  */
        Interval::I X, Y, Z;
        Type type;
    };

    /*  Indices of X, Y, Z coordinates */
    Clause::Id X, Y, Z;

    /*  Constants, unpacked from the tree at construction */
    std::map<Clause::Id, float> constants;

    /*  Tape containing our opcodes in reverse order */
    std::list<Subtape> tapes;
    std::list<Subtape>::iterator tape;

    /*  Map of variables (in terms of where they live in this Evaluator) to
     *  their ids in their respective Tree (e.g. what you get when calling
     *  Tree::var().id() */
    boost::bimap<Clause::Id, Tree::Id> vars;

    /*  Used when pushing into the tape  */
    std::vector<uint8_t> disabled;
    std::vector<Clause::Id> remap;

public:

    /*
     *  Pushes a new tape onto the stack, storing it in tape
     *
     *  E must include a function check(Clause::Id) ->  Keep
     *  E::TapeType must be a member of Type
     *  E must include a function bounds(Interval& X, Interval& Y, Interval& Z)
     *  which records its last known bounds (used for the tape push)
     */
    template <class E>
    void push(const E& e)
    {
        // Since we'll be figuring out which clauses are disabled and
        // which should be remapped, we reset those arrays here
        std::fill(disabled.begin(), disabled.end(), true);
        std::fill(remap.begin(), remap.end(), 0);

        // Mark the root node as active
        disabled[tape->i] = false;

        for (const auto& c : tape->t)
        {
            if (!disabled[c.id])
            {
                switch (e.check(c.id))
                {
                    case KEEP_A:    disabled[c.a] = false;
                                    remap[c.id] = c.a;
                                    break;
                    case KEEP_B:    disabled[c.b] = false;
                                    remap[c.id] = c.b;
                                    break;
                    case KEEP_BOTH: break;

                }

                if (!remap[c.id])
                {
                    disabled[c.a] = false;
                    disabled[c.b] = false;
                }
                else
                {
                    disabled[c.id] = true;
                }
            }
        }

        auto prev_tape = tape;

        // Add another tape to the top of the tape stack if one doesn't already
        // exist (we never erase them, to avoid re-allocating memory during
        // nested evaluations).
        if (++tape == tapes.end())
        {
            tape = tapes.insert(tape, Subtape());
            tape->t.reserve(tapes.front().t.size());
        }
        else
        {
            // We may be reusing an existing tape, so resize to 0
            // (preserving allocated storage)
            tape->t.clear();
        }

        assert(tape != tapes.end());
        assert(tape != tapes.begin());
        assert(tape->t.capacity() >= prev_tape->t.size());

        // Reset tape type
        tape->type = E::TapeType;

        // Now, use the data in disabled and remap to make the new tape
        for (const auto& c : prev_tape->t)
        {
            if (!disabled[c.id])
            {
                Clause::Id ra, rb;
                for (ra = c.a; remap[ra]; ra = remap[ra]);
                for (rb = c.b; remap[rb]; rb = remap[rb]);
                tape->t.push_back({c.op, c.id, ra, rb});
            }
        }

        // Remap the tape root index
        for (tape->i = prev_tape->i; remap[tape->i]; tape->i = remap[tape->i]);

        // Make sure that the tape got shorter
        assert(tape->t.size() <= prev_tape->t.size());

        // Store X / Y / Z bounds (may be irrelevant)
        e.setBounds(tape->X, tape->Y, tape->Z);
    }

    /*
     *  Walks the tape in bottom-to-top order
     *  Returns the clause id of the root.
     *
     *  E must expose evalClause(Opcode, Clause::Id, Clause::Id)
     */
    template <class E>
    Clause::Id walk(E& e)
    {
        for (auto itr = tape->t.rbegin(); itr != tape->t.rend(); ++itr)
        {
            e.evalClause(itr->op, itr->a, itr->b);
        }
        return tape->i;
    }
};

}   // namespace Kernel
