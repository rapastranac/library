#ifdef VC_VOID_MPI

#include "VertexCover.hpp"

auto user_serializer = [](auto &...args) {
	/* here inside, user can implement its favourite serialization method given the
	arguments pack and it must return a std::stream */
	std::stringstream ss;
	cereal::BinaryOutputArchive archive(ss);
	archive(args...);
	return std::move(ss);
};

auto user_deserializer = [](std::stringstream &ss, auto &...args) {
	/* here inside, the user can implement its favourite deserialization method given buffer
	and the arguments pack*/
	cereal::BinaryInputArchive archive(ss);
	archive(args...);
};

class VC_void_MPI : public VertexCover
{
	using HolderType = library::ResultHolder<void, int, Graph>;

private:
	std::function<void(int, int, Graph &, HolderType *)> _f;

public:
	VC_void_MPI()
	{
		this->_f = std::bind(&VC_void_MPI::mvc, this, _1, _2, _3, _4);
	}
	~VC_void_MPI() {}

	void mvc(int id, int depth, Graph &graph, HolderType *parent)
	{
		size_t k1 = graph.min_k();
		size_t k2 = graph.max_k();
		size_t k = relaxation(k1, k2);

		if (k + graph.coverSize() >= branchHandler.getRefValue())
		{
			size_t addition = k + graph.coverSize();
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
		Graph gLeft = graph;			 /*Let gLeft be a copy of graph*/
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
		gLeft.clean_graph();
		int C1Size = (int)gLeft.coverSize();
		gRight.removeNv(v);
		gRight.clean_graph();
		int C2Size = (int)gRight.coverSize();
		hol_r.holdArgs(newDepth, gRight);
		//*******************************************************************************************

		if (C1Size < branchHandler.getRefValue())
		{
			hol_l.holdArgs(newDepth, gLeft);
#ifdef DLB
			branchHandler.push_multiprocess<void>(_f, id, hol_l, user_serializer, true);
#else
			branchHandler.push_multiprocess<void>(_f, id, hol_l, user_serializer);
#endif
		}

		if (C2Size < branchHandler.getRefValue() || hol_r.isBound())
		{
#ifdef DLB
			branchHandler.forward<void>(_f, id, hol_r, true);
#else
			branchHandler.forward<void>(_f, id, hol_r);
#endif
		}

		return;
	}

	void terminate_condition(Graph &graph, int id, int depth)
	{
		auto condition1 = [this](int refValGlobal, int refValLocal) {
			return (leaves == 0) && (refValLocal < refValGlobal) ? true : false;
		};
		//if condition1 complies, then ifCond1 is called
		auto ifCond1 = [&]() {
			foundAtDepth = depth;
			string col1 = fmt::format("MVC found so far has {} elements", branchHandler.getRefValue());
			string col2 = fmt::format("thread {}", id);
			cout << std::internal
				 << std::setfill('.')
				 << col1
				 << std::setw(wide - col1.size())
				 << col2
				 << "\n";

			outFile(col1, col2);
			++leaves;
		};

		auto condition2 = [](int refValGlobal, int refValLocal) {
			return refValLocal < refValGlobal ? true : false;
		};

		auto ifCond2 = [&]() {
			foundAtDepth = depth;
			string col1 = fmt::format("MVC found so far has {} elements", branchHandler.getRefValue());
			string col2 = fmt::format("thread {}", id);
			cout << std::internal
				 << col1
				 << std::setw(wide - col1.size())
				 << col2
				 << "\n";

			outFile(col1, col2);
			if (depth > measured_Depth)
			{
				measured_Depth = depth;
			}

			++leaves;
		};

		branchHandler.replaceIf<void>(graph.coverSize(), condition1, &ifCond1, graph, user_serializer); // thread safe
		branchHandler.replaceIf<void>(graph.coverSize(), condition2, &ifCond2, graph, user_serializer);

		return;
	}
};

#endif