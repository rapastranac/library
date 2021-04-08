#ifdef VC_VOID_MPI

#include "VertexCover.hpp"

void helper_ser(auto &archive, auto &first)
{
	archive << first;
}

void helper_ser(auto &archive, auto &first, auto &...args)
{
	archive << first;
	helper_ser(archive, args...);
}

auto user_serializer = [](std::stringstream &ss, auto &&...args) {
	/* here inside, user can implement its favourite serialization method given the
	arguments pack and it must return a std::stream */
	//cereal::BinaryOutputArchive archive(ss);
	//archive(args...);
	boost::archive::text_oarchive archive(ss);
	helper_ser(archive, args...);
};

void helper_dser(auto &archive, auto &first)
{
	archive >> first;
}

void helper_dser(auto &archive, auto &first, auto &...args)
{
	archive >> first;
	helper_dser(archive, args...);
}

auto user_deserializer = [](std::stringstream &ss, auto &...args) {
	/* here inside, the user can implement its favourite deserialization method given buffer
	and the arguments pack*/
	//cereal::BinaryInputArchive archive(ss);
	boost::archive::text_iarchive archive(ss);

	helper_dser(archive, args...);
	//archive(args...);
};

class VC_void_MPI : public VertexCover
{
	using HolderType = library::ResultHolder<void, int, Graph>;

private:
	std::function<void(int, int, Graph, void *)> _f;

public:
	VC_void_MPI()
	{
		this->_f = std::bind(&VC_void_MPI::mvc, this, _1, _2, _3, _4);
	}
	~VC_void_MPI() {}

	void mvc(int id, int depth, Graph graph, void *parent)
	{
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

		if (hol_l.evaluate_branch_checkIn())
		{
#ifdef DLB
			branchHandler.push_multiprocess<void>(_f, id, hol_l, user_serializer, true);
#else
			branchHandler.push_multiprocess<void>(_f, id, hol_l, user_serializer);
#endif
		}
		/*
		if (C2Size < branchHandler.getRefValue() || hol_r.isBound())
		{*/
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

	void terminate_condition(Graph &graph, int id, int depth)
	{
		auto condition1 = [this](int refValGlobal, int refValLocal) {
			return (leaves == 0) && (refValLocal < refValGlobal) ? true : false;
		};
		//if condition1 complies, then ifCond1 is called
		auto ifCond1 = [&]() {
			foundAtDepth = depth;
			measured_Depth = depth;
			recurrent_msg(id);
			++leaves;
		};

		auto condition2 = [](int refValGlobal, int refValLocal) {
			return refValLocal < refValGlobal ? true : false;
		};

		auto ifCond2 = [&]() {
			foundAtDepth = depth;
			recurrent_msg(id);

			if (depth > (int)measured_Depth)
				measured_Depth = (size_t)depth;

			++leaves;
		};

		branchHandler.replace_refValGlobal_If<void>(graph.coverSize(), condition1, ifCond1, graph, user_serializer); // thread safe
		branchHandler.replace_refValGlobal_If<void>(graph.coverSize(), condition2, ifCond2, graph, user_serializer); // thread safe

		return;
	}
};

#endif