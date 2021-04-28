#ifdef VC_VOID

#include "VertexCover.hpp"

class VC_void : public VertexCover
{
    using HolderType = library::ResultHolder<void, int, Graph>;

private:
    std::function<void(int, int, Graph &, void *)> _f;

public:
    VC_void()
    {
        this->_f = std::bind(&VC_void::mvc, this, _1, _2, _3, _4);
    }
    ~VC_void() {}

    bool findCover(int run)
    {
        string msg_center = fmt::format("run # {} ", run);
        msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
        cout << msg_center;
        outFile(msg_center, "");

        this->branchHandler.setMaxThreads(numThreads);
        this->branchHandler.functionIsVoid();

        //size_t _k_mm = maximum_matching(graph);
        //size_t _k_uBound = graph.max_k();
        //size_t _k_lBound = graph.min_k();
        //size_t k_prime = std::min(k_mm, k_uBound);
        //currentMVCSize = k_prime;
        //preSize = graph.size();

        preSize = graph.preprocessing();

        auto degLB = graph.DegLB();

        size_t k_mm = maximum_matching(graph);
        size_t k_uBound = graph.max_k();
        size_t k_lBound = graph.min_k();
        size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
        currentMVCSize = k_prime;

        begin = std::chrono::steady_clock::now();

        try
        {
            branchHandler.setRefValue(currentMVCSize);
            //mvc(-1, 0, graph);
            //testing ****************************************
            HolderType initial(branchHandler, -1);
            {
                int depth = 0;
                initial.holdArgs(depth, graph);
                branchHandler.push_multithreading<void>(_f, -1, initial);
                //mvc(-1, 0, graph, nullptr);
            }
            //************************************************

            branchHandler.wait();
            graph_res = branchHandler.fetchSolution<Graph>();
            graph_res2 = graph_res;
            cover = graph_res.postProcessing();
        }
        catch (std::exception &e)
        {
            this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
            if (!output.is_open())
            {
                printf("Error, output file not found ! \n");
            }
            std::cout << "Exception caught : " << e.what() << '\n';
            output << "Exception caught : " << e.what() << '\n';
            output.close();
        }

        cout << "DONE!" << endl;
        end = std::chrono::steady_clock::now();
        elapsed_secs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

        printf("refGlobal : %d \n", branchHandler.getRefValue());
        return true;
    }

    void mvc(int id, int depth, Graph &graph, void *parent)
    {

        //vector<Graph> graphs;
        //for (int i = 0; i < 500; i++)
        //{
        //    Graph gg = graph;
        //    graphs.push_back(gg);
        //    cout << "done=" << i << endl;
        //}

        size_t LB = graph.min_k();
        size_t degLB = 0; //graph.DegLB();
        size_t UB = graph.max_k();
        size_t acLB = 0; //graph.antiColoringLB();
        //size_t mm = maximum_matching(graph);
        size_t k = relaxation(LB, UB);
        //std::max({LB, degLB, acLB})

        if (k + graph.coverSize() >= (size_t)branchHandler.getRefValue())
        {
            //size_t addition = k + graph.coverSize();
            return;
        }

        if (graph.size() == 0)
        {
#ifdef DEBUG_COMMENTS
            printf("Leaf reached, depth : %d \n", depth);
#endif
            terminate_condition(graph, id, depth);
            return;
        }

        int v = graph.id_max(false);
        HolderType hol_l(branchHandler, id, parent);
        HolderType hol_r(branchHandler, id, parent);
        hol_l.setDepth(depth);
        hol_r.setDepth(depth);
#ifdef DLB
        branchHandler.linkParent(id, parent, hol_l, hol_r);
#endif

        int *referenceValue = branchHandler.getRefValueTest();

        hol_l.bind_branch_checkIn([&graph, &v, referenceValue, &depth, &hol_l] {
            Graph g = graph;
            g.removeVertex(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int C = g.coverSize();
            if (C < referenceValue[0]) // user's condition to see if it's worth it to make branch call
            {
                int newDepth = depth + 1;
                hol_l.holdArgs(newDepth, g);
                return true; // it's worth it
            }
            else
                return false; // it's not worth it
        });

        hol_r.bind_branch_checkIn([&graph, &v, referenceValue, &depth, &hol_r] {
            Graph g = std::move(graph);
            g.removeNv(v);
            g.clean_graph();
            //g.removeZeroVertexDegree();
            int C = g.coverSize();
            if (C < referenceValue[0]) // user's condition to see if it's worth it to make branch call
            {
                int newDepth = depth + 1;
                hol_r.holdArgs(newDepth, g);
                return true; // it's worth it
            }
            else
                return false; // it's not worth it
        });

        //*******************************************************************************************

        if (hol_l.evaluate_branch_checkIn())
        {
#ifdef DLB
            branchHandler.push_multithreading<void>(_f, id, hol_l, true);
#else
            branchHandler.push_multithreading<void>(_f, id, hol_l);
#endif
        }

        if (hol_r.evaluate_branch_checkIn())
        {
#ifdef DLB
            branchHandler.forward<void>(_f, id, hol_r, true);
#else
            branchHandler.forward<void>(_f, id, hol_r);
#endif
        }

        return;
    }

private:
    void terminate_condition(Graph &graph, int id, int depth)
    {
        auto condition1 = [this](int refValGlobal, int refValLocal) {
            return (leaves == 0) && (refValLocal < refValGlobal) ? true : false;
        };
        //if condition1 complies, then ifCond1 is called
        auto ifCond1 = [&]() {
            foundAtDepth = depth;
            recurrent_msg(id);
            ++leaves;
        };

        auto condition2 = [](int refValGlobal, int refValLocal) {
            return refValLocal < refValGlobal ? true : false;
        };

        auto ifCond2 = [&]() {
            foundAtDepth = depth;
            recurrent_msg(id);

            if (depth > measured_Depth)
            {
                measured_Depth = depth;
            }

            ++leaves;
        };

        branchHandler.replace_refValGlobal_If<void>(graph.coverSize(), condition1, ifCond1, graph); // thread safe
        branchHandler.replace_refValGlobal_If<void>(graph.coverSize(), condition2, ifCond2, graph);

        return;
    }
};

#endif