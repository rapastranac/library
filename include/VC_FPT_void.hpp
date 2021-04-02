#ifdef VC_FPT_VOID

#include "VertexCover.hpp"

class VC_FPT_void : public VertexCover
{
    using HolderType = library::ResultHolder<void, int, int, Graph>;

    int FPT_init = -1;

private:
    std::function<void(int, int, int, Graph &, void *)> _f;

public:
    VC_FPT_void()
    {
        this->_f = std::bind(&VC_FPT_void::mvc, this, _1, _2, _3, _4, _5);
    }
    ~VC_FPT_void() {}

    bool findCover(int run)
    {
        string msg_center = fmt::format("run # {} ", run);
        msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
        cout << msg_center;
        outFile(msg_center, "");

        this->branchHandler.setMaxThreads(numThreads);
        this->branchHandler.functionIsVoid();

        preSize = graph.preprocessing();

        size_t k_mm = maximum_matching(graph);
        size_t k_uBound = graph.max_k();
        size_t k_lBound = graph.min_k();
        int k_prime = std::min(k_mm, k_uBound);
        //k_prime = 946;
        currentMVCSize = k_prime;

        begin = std::chrono::steady_clock::now();

        try
        {
            for (int k = k_prime; k > k_lBound; k--)
            {
                FPT_init = k;
                branchHandler.setRefValue(0); // resets flag to 0
                auto g = graph;
                HolderType initial(branchHandler, -1);
                {
                    int depth = 0;
                    initial.holdArgs(k, depth, g);
                    branchHandler.push_multithreading<void>(_f, -1, initial);
                }
                //************************************************

                branchHandler.wait(); // acts like a barrier
                if (branchHandler.has_result())
                {
                    graph_res = branchHandler.retrieveResult<Graph>();
                    branchHandler.clear_result();
                    cover = graph_res.postProcessing();
                    fmt::print("MVC {} found for k = {}, {}\n", cover.size(), k, true);
                }
                else
                    fmt::print("MVC not found for k = {}, {}\n", k, false);
                //graph_res2 = graph_res;
                //cover = graph_res.postProcessing();
            }
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

    void mvc(int id, int k, int depth, Graph &graph, void *parent)
    {
        if (branchHandler.getRefValue() == 1)
            return;

        if (k < 0)
            return;

        size_t LB = graph.min_k();
        //size_t degLB = graph.DegLB();
        size_t UB = graph.max_k();
        //size_t k = relaxation(k1, k2);

        if (graph.size() > (k * (k - 1)) || graph.getNumEdges() > (k * k))
            return;

        if (graph.coverSize() + LB > (size_t)currentMVCSize)
            return;

        if (k == 0)
        {
            if (graph.getNumEdges() == 0)
            {
                terminate_condition(graph, id, k, depth);
                return;
            }
            else
                return;
        }

        if (graph.getNumEdges() == 0)
        {
#ifdef DEBUG_COMMENTS
            printf("Leaf reached, depth : %d \n", depth);
#endif
            terminate_condition(graph, id, k, depth);
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

        hol_l.bind_branch_checkIn([&k, &graph, &v, referenceValue, &depth, &hol_l] {
            if (referenceValue[0] == 1)
                return false;

            Graph g = graph;
            g.removeVertex(v);
            int ki = g.clean_graph(k);
            //g.removeZeroVertexDegree();
            int newDepth = depth + 1;
            int newK = k - ki - 1;
            hol_l.holdArgs(newK, newDepth, g);
            return true; // it's worth it
        });

        hol_r.bind_branch_checkIn([&k, &graph, &v, referenceValue, &depth, &hol_r] {
            if (referenceValue[0] == 1)
                return false;

            Graph g = std::move(graph);
            int N = g.removeNv(v);
            int ki = g.clean_graph(k);
            //g.removeZeroVertexDegree();
            int newDepth = depth + 1;
            int newK = k - ki - N;
            hol_r.holdArgs(newK, newDepth, g);
            return true; // it's worth it
        });

        // left recursion ***************************************************************************
        if (hol_l.evaluate_branch_checkIn())
        {
#ifdef DLB
            branchHandler.push_multithreading<void>(_f, id, hol_l, true);
#else
            branchHandler.push_multithreading<void>(_f, id, hol_l);
#endif
        }
        //*******************************************************************************************

        // right recursion **************************************************************************
        if (hol_r.evaluate_branch_checkIn())
        {
#ifdef DLB
            branchHandler.forward<void>(_f, id, hol_r, true);
#else
            branchHandler.forward<void>(_f, id, hol_r);
#endif
        }
        //*******************************************************************************************

        return;
    }

private:
    void terminate_condition(Graph &graph, int id, int k, int depth)
    {
        auto condition = [this](int refValGlobal, int refValLocal) {
            return refValLocal != refValGlobal ? true : false;
        };
        branchHandler.replace_refValGlobal_If<void>(1, condition, nullptr, graph); // thread safe
        currentMVCSize = graph.coverSize();

        return;
    }
};

#endif