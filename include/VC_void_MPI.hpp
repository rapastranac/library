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

auto serializer = [](auto &&...args) {
	/* here inside, user can implement its favourite serialization method given the
	arguments pack and it must return a std::string */
	std::stringstream ss;
	//cereal::BinaryOutputArchive archive(ss);
	//archive(args...);
	boost::archive::text_oarchive archive(ss);
	helper_ser(archive, args...);
	return ss.str();
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

auto deserializer = [](std::stringstream &ss, auto &...args) {
	/* here inside, the user can implement its favourite deserialization method given buffer
	and the arguments pack*/
	//cereal::BinaryInputArchive archive(ss);
	boost::archive::text_iarchive archive(ss);

	helper_dser(archive, args...);
	//archive(args...);
};

class VC_void_MPI : public VertexCover
{
	using HolderType = GemPBA::ResultHolder<void, int, Graph>;

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

		if (k + graph.coverSize() >= (size_t)branchHandler.refValue())
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
		int SIZE = graph.size();

		HolderType hol_l(dlb, id, parent);
		HolderType hol_r(dlb, id, parent);
		hol_l.setDepth(depth);
		hol_r.setDepth(depth);
#ifdef R_SEARCH
		dlb.linkParent(id, parent, hol_l, hol_r);
#endif

		hol_l.bind_branch_checkIn([&] {
			Graph g = graph;
			g.removeVertex(v);
			g.clean_graph();
			//g.removeZeroVertexDegree();
			int C = g.coverSize();
			if (C < branchHandler.refValue()) // user's condition to see if it's worth it to make branch call
			{
				int newDepth = depth + 1;
				hol_l.holdArgs(newDepth, g);
				return true; // it's worth it
			}
			else
				return false; // it's not worth it
		});

		hol_r.bind_branch_checkIn([&] {
			Graph g = std::move(graph);
			g.removeNv(v);
			g.clean_graph();
			//g.removeZeroVertexDegree();
			int C = g.coverSize();
			if (C < branchHandler.refValue()) // user's condition to see if it's worth it to make branch call
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
			//if (SIZE > 15)
			branchHandler.try_push_MP<void>(_f, id, hol_l, serializer);
			//else
			//branchHandler.try_push_MT<void>(_f, id, hol_l);
		}
		if (hol_r.evaluate_branch_checkIn())
		{
			branchHandler.forward<void>(_f, id, hol_r);
		}

		return;
	}

	void terminate_condition(Graph &graph, int id, int depth)
	{
		std::scoped_lock<std::mutex> lck(mtx);
		if (graph.coverSize() < branchHandler.refValue())
		{
			branchHandler.holdSolution(graph.coverSize(), graph, serializer);

			branchHandler.updateRefValue(graph.coverSize());
			foundAtDepth = depth;
			recurrent_msg(id);

			if (depth > (int)measured_Depth)
				measured_Depth = (size_t)depth;

			++leaves;
		}

		return;
	}
};

#endif