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
        int k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
        k_prime = 174;
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
                    cover = graph_res.postProcessing();
                    fmt::print("MVC {} found for k = {}, {} \n", cover.size(), k, true);
                }
                else
                    fmt::print("MVC not found for k = {}, {}", k, false);
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

        if (k < 0 || branchHandler.getRefValue() == 1)
            return;
        if (graph.coverSize() == FPT_init && graph.getNumEdges() == 0)
        {
#ifdef DEBUG_COMMENTS
            printf("Leaf reached, depth : %d \n", depth);
#endif
            terminate_condition(graph, id, k, depth);
            return;
        }
        if (graph.getNumEdges() == 0)
            return;

        Graph gLeft = graph;             /*Let gLeft be a copy of graph*/
        Graph gRight = std::move(graph); // graph;	/*Let gRight be a copy of graph*/
        int newDepth = depth + 1;

        int v = gLeft.id_max(false);
        HolderType hol_l(branchHandler, id, parent);
        HolderType hol_r(branchHandler, id, parent);
        hol_l.setDepth(depth);
        hol_r.setDepth(depth);
#ifdef DLB
        branchHandler.linkParent(id, parent, hol_l, hol_r);
#endif
        gLeft.removeVertex(v); //perform deletion before checking if worth to explore branch
        gLeft.removeZeroVertexDegree();
        int k_l = k - 1;
        //gLeft.clean_graph();
        //int C1Size = (int)gLeft.coverSize();
        int k_r = k - gRight.removeNv(v);
        gRight.removeZeroVertexDegree();
        //gRight.clean_graph();
        //int C2Size = (int)gRight.coverSize();
        hol_r.holdArgs(k_r, newDepth, gRight);
        //*******************************************************************************************

        // left recursion ***************************************************************************
        hol_l.holdArgs(k_l, newDepth, gLeft);
#ifdef DLB
        branchHandler.push_multithreading<void>(_f, id, hol_l, true);
#else
        branchHandler.push_multithreading<void>(_f, id, hol_l);
#endif
        //*******************************************************************************************

        // right recursion **************************************************************************
#ifdef DLB
        branchHandler.forward<void>(_f, id, hol_r, true);
#else
        branchHandler.forward<void>(_f, id, hol_r);
#endif
        //*******************************************************************************************

        return;
    }

private:
    void terminate_condition(Graph &graph, int id, int k, int depth)
    {
        auto condition = [this](int refValGlobal, int refValLocal) {
            return refValLocal != refValGlobal ? true : false;
        };
        //if condition is met, then ifCond is called
        /*auto ifCond = [&]() {
            foundAtDepth = depth;
            auto res = graph.postProcessing();
            string col1 = fmt::format("MVC found so far has {} elements", res.size());
            string col2 = fmt::format("process {}, thread {}", branchHandler.getRankID(), id);
            cout << std::internal
                 << std::setfill('.')
                 << col1
                 << std::setw(wide - col1.size())
                 << col2
                 << "\n";

            outFile(col1, col2);
            ++leaves;
        }; */

        branchHandler.replace_refValGlobal_If<void>(1, condition, nullptr, graph); // thread safe

        return;
    }
};

#endif