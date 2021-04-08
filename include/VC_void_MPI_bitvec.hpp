#ifdef VC_VOID_MPI

#include "VertexCover.hpp"


#include <boost/dynamic_bitset.hpp>
#include <boost/container/set.hpp>
#include <boost/unordered_set.hpp>

using namespace boost;


#define gbitset dynamic_bitset<>



namespace boost { namespace serialization {

    template <typename Ar, typename Block, typename Alloc>
        void save(Ar& ar, dynamic_bitset<Block, Alloc> const& bs, unsigned) {
            size_t num_bits = bs.size();
            std::vector<Block> blocks(bs.num_blocks());
            to_block_range(bs, blocks.begin());
            ar & num_bits & blocks;
        }

    template <typename Ar, typename Block, typename Alloc>
        void load(Ar& ar, dynamic_bitset<Block, Alloc>& bs, unsigned) {
            size_t num_bits;
            std::vector<Block> blocks;
            ar & num_bits & blocks;

            bs.resize(num_bits);
            from_block_range(blocks.begin(), blocks.end(), bs);
            bs.resize(num_bits);
        }

    template <typename Ar, typename Block, typename Alloc>
        void serialize(Ar& ar, dynamic_bitset<Block, Alloc>& bs, unsigned version) {
            split_free(ar, bs, version);
        }

} }


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
	using HolderType = library::ResultHolder<void, int, gbitset, gbitset>;

private:
    //std::function<void(int, int, Graph &, void *)> _f;
    std::function<void(int, int, gbitset, gbitset, void *)> _f;

public:

	    vector<boost::unordered_set<pair<gbitset,int>>> seen;
	    long is_skips;
	    long deglb_skips;
	    long seen_skips;
	    
	    unordered_map<int, gbitset> graphbits;
	    long passes;

	VC_void_MPI()
	{
		this->_f = std::bind(&VC_void_MPI::mvcbitset, this, _1, _2, _3, _4, _5);
	}
	~VC_void_MPI() {}




	void setGraph(Graph &graph)
	{
		is_skips = 0;
    		deglb_skips = 0;
    		seen_skips = 0;
		//this->branchHandler.setMaxThreads(numThreads);
		this->branchHandler.functionIsVoid();
 
		for (int i = 0; i <= numThreads; i++)
		{
			seen.push_back(boost::unordered_set<pair<gbitset,int>>());
		}
		
			
		
		passes = 0;
		int gsize = graph.adj.size() + 1;	//+1 cuz some files use node ids from 1 to n (instead of 0 to n - 1)

		cout<<"Graph has "<<graph.adj.size()<<" vertices and "<<graph.getNumEdges()<<" edges"<<endl;
		vector<pair<int,int>> deg_v;
		for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it)
		{
			deg_v.push_back(make_pair(it->second.size(), it->first));
		}		
		std::sort(deg_v.begin(), deg_v.end());
		map<int, int> remap;
		for (int i = 0; i < deg_v.size(); i++)
		{
			remap[deg_v[i].second] = deg_v.size() - 1 - i;
		}
		map<int, set<int>> adj2;
		for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it)
		{
			int v = it->first;
			adj2[remap[v]] = set<int>();
			for (auto w : graph.adj[v])
			{
				adj2[remap[v]].insert(remap[w]);
			}
		}
		
		

		//for (auto it = graph.adj.begin(); it != graph.adj.end(); ++it)
		for (auto it = adj2.begin(); it != adj2.end(); ++it)
		{
			int v = it->first;

			gbitset vnbrs(gsize);

			for (int i : it->second)
			{
				vnbrs[i] = true;
			}
			graphbits[v] = vnbrs;
		}
		
		//check for evil degree 0 vertices
		for (int i = 0; i < gsize; ++i)
		{
			if (!graphbits.contains(i))
			{
				graphbits[i] = gbitset(gsize);
			}
		}

		

	}
	
	
	
	
	
	void mvcbitset(int id, int depth, gbitset bits_in_graph, gbitset cur_sol, void *parent)
	{
		passes++;
		
		
		
		/*cout<<bits_in_graph.size()<<endl;
		cout<<cur_sol.size()<<endl;
		cout<<graphbits.size()<<endl;
		cout<<graphbits[0].size()<<endl;*/
		
		int cursol_size = cur_sol.count();
		
		if (passes % 1000000 == 0)
		{
			cout<<"passes="<<passes<<" gsize="<<bits_in_graph.count()<<" refvalue="<<branchHandler.getRefValue()<<" solsize="<<cur_sol.count()<<" isskips="<<is_skips<<" deglbskips="<<deglb_skips<<" seen_skips="<<seen_skips<<" seen.size="<<seen[id].size()<<endl;
			//cout<<"ID="<<id<<" CSOL="<<cursol_size<<" REFVAL="<<branchHandler.getRefValue()<<endl;
			
		}
		
		

		//cout<<"depth="<<depth<<" ref="<<branchHandler.getRefValue()<<"cursolsize="<<cursol_size<<" cnt="<<bits_in_graph.count()<<" sol="<<cur_sol<<endl;
		if (bits_in_graph.count() <= 1)
		{
			
			terminate_condition_bits(cur_sol, cursol_size, id, depth);
			return;
		}

		
		if (cursol_size >= branchHandler.getRefValue())
		{
			return;
		}
		
		
		if (bits_in_graph.count() <= 90 && bits_in_graph.count() >= 120 )
		{
			pair<gbitset, int> instance_key = make_pair(bits_in_graph, cursol_size);
			if (seen[id].find(instance_key) != seen[id].end())
			{
				seen_skips++;
				return;
			}
			
			if (seen[id].size() <= 4000000)
			{
				seen[id].insert(instance_key);
			}
		}
			
		
		//max degree dude
		int maxdeg = 0;
		int maxdeg_v = 0;


		int nbEdgesDoubleCounted = 0;

		

		bool someRuleApplies = true;
	
		while (someRuleApplies)
		{
			nbEdgesDoubleCounted = 0;
			maxdeg = -1;
			maxdeg_v = 0;
			someRuleApplies = false;

		

			for (int i = bits_in_graph.find_first(); i != gbitset::npos; i = bits_in_graph.find_next(i))
			{
			
				gbitset nbrs = (graphbits[i] & bits_in_graph);

				int cnt = nbrs.count();
				if (cnt == 0)
				{
					bits_in_graph[i] = false;
				}
				else if (cnt == 1)
				{
					int the_nbr = nbrs.find_first();
					cur_sol[the_nbr] = true;
					cursol_size++;
					bits_in_graph[i] = false;
					bits_in_graph[the_nbr] = false;
					someRuleApplies = true;
				}
				else if (cnt > maxdeg)
				{
					maxdeg = cnt;
					maxdeg_v = i;
				}
				else
				{
					//looking for a nbr with at least same nbrs as i
					if (cnt == 2)
					{
						int n1 = nbrs.find_first();
						int n2 = nbrs.find_next(n1);
						if (graphbits[n1][n2])
						{
							cur_sol[n1] = true;
							cur_sol[n2] = true;
							bits_in_graph[i] = false;
							bits_in_graph[n1] = false;
							bits_in_graph[n2] = false;
							cursol_size += 2;
							someRuleApplies = true;
						}
					
					}
					/*
					{
						for (int j = nbrs.find_first(); j != gbitset::npos; j = nbrs.find_next(j))
						{
							gbitset nbrs_of_j = (graphbits[j] & bits_in_graph);
							nbrs_of_j.set(i, false);
							nbrs_of_j.set(j, true);
							if ((nbrs_of_j & nbrs) == nbrs)
							{
								//cout<<"we have twins at (i, j)="<<i<<","<<j<<endl<<
								//	nbrs<<endl<<(graphbits[j] & bits_in_graph)<<endl<<(nbrs_of_j & nbrs)<<endl;
								cur_sol[j] = true;
								cursol_size++;
								//bits_in_graph[i] = false;
								bits_in_graph[j] = false;
								someRuleApplies = true;
								break;
							}
							
						}
					}*/
				
				}
				nbEdgesDoubleCounted += cnt;			
		
			}
		}





		int nbVertices = bits_in_graph.count(); 
		if (nbVertices <= 1)
		{
			//cout<<"terminating 2"<<endl;
			terminate_condition_bits(cur_sol, cursol_size, id, depth);
			return;
		}
		
		
		float tmp = (float)(1 - 8 * nbEdgesDoubleCounted/2 - 4*nbVertices + 4*nbVertices*nbVertices);
		int indsetub = (int)(0.5f * (1.0f + sqrt(tmp)));
		int vclb = nbVertices - indsetub;

		if (vclb + cursol_size >= branchHandler.getRefValue())
		{
			is_skips++;
			return;
		}
		
		
		
		int degLB = 0;//getDegLB(bits_in_graph, nbEdgesDoubleCounted/2);
		degLB = (nbEdgesDoubleCounted/2)/maxdeg;
		//cout<<"deglb="<<degLB<<" n="<<bits_in_graph.count()<<" refval="<<branchHandler.getRefValue()<<endl;
		if (degLB + cursol_size >= branchHandler.getRefValue())
		{
			deglb_skips++;
			return;
		}
		

		/*if (maxdeg <= 2)
		{
			//TODO : ACTUALLY COMPUTE THE CYCLES
			terminate_condition_bits(cur_sol, cursol_size + nbVertices/2, id, depth);
			//cout<<"MAXDEG 2 n="<<nbVertices<<endl;
			return;
		}*/

		
		int newDepth = depth + 1;
		
		HolderType hol_l(branchHandler, id, parent);
		HolderType hol_r(branchHandler, id, parent);
		
		hol_l.setDepth(depth);
		hol_r.setDepth(depth);
		
		int *referenceValue = branchHandler.getRefValueTest();
		
		hol_l.bind_branch_checkIn([this, &bits_in_graph, &maxdeg_v, &cursol_size, &cur_sol, &newDepth, referenceValue, &depth, &hol_l] {
			gbitset ingraph1 = bits_in_graph;
			ingraph1.set(maxdeg_v, false);
			gbitset sol1 = cur_sol;
			sol1.set(maxdeg_v, true);
			int solsize1 = cursol_size + 1;
			
			if (solsize1 < referenceValue[0])
			{
				hol_l.holdArgs(newDepth, ingraph1, sol1);
				return true;
			}
			else
				return false;
		
		});
		
		
		
		hol_r.bind_branch_checkIn([this, &bits_in_graph, &maxdeg_v, &cursol_size, &cur_sol, &newDepth, referenceValue, &depth, &hol_r] {
			//right branch = take out v nbrs
			gbitset ingraph2 = bits_in_graph;

			ingraph2 = bits_in_graph & (~graphbits[maxdeg_v]);
			gbitset nbrs = (graphbits[maxdeg_v] & bits_in_graph);
			gbitset sol2 = cur_sol | nbrs;	//add all nbrs to solution
			int solsize2 = cursol_size + nbrs.count();
			
			if (solsize2 < referenceValue[0])
			{
				hol_r.holdArgs(newDepth, ingraph2, sol2);
				return true;
			}
			else
				return false;
		
		});		
		
		
		
		
		
		
		
		
		
		
		
	#ifdef DLB
		branchHandler.linkParent(id, parent, hol_l, hol_r);
	#endif

	
		if (hol_l.evaluate_branch_checkIn())
		{
		    
#ifdef DLB
			if (nbVertices <= 20)
				branchHandler.push_multithreading<void>(_f, id, hol_l, true);
			else
				branchHandler.push_multiprocess<void>(_f, id, hol_l, user_serializer, true);
#else
			branchHandler.push_multiprocess<void>(_f, id, hol_l, user_serializer);
#endif
		  
		}

		//cout<<"ok sol1 done, going to sol2 val="<<solsize2;

		
		
		if (hol_r.evaluate_branch_checkIn()){
	#ifdef DLB
		    branchHandler.forward<void>(_f, id, hol_r, true);
	#else
		    branchHandler.forward<void>(_f, id, hol_r);
	#endif
		}
		else
		{
			//cout<<" (did not actually go)"<<endl;
		}
		
		//cout<<"depth="<<depth<<" done"<<endl;
		
		return;
	
	
	} 

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    

private:

    
	
	void terminate_condition_bits(gbitset &cur_sol, int solsize, int id, int depth)
	{
		if (solsize == 0)
			return;
		auto condition1 = [this](int refValGlobal, int refValLocal) {
			return (leaves == 0) && (refValLocal < refValGlobal) ? true : false;
		};
		//if condition1 complies, then ifCond1 is called
		auto ifCond1 = [&]() {
			foundAtDepth = depth;
			string col1 = fmt::format("MVC found so far has {} elements", branchHandler.getRefValue());
			string col2 = fmt::format("process {}, thread {}", branchHandler.getRankID(), id);
			cout << std::internal
				 << std::setfill('.')
				 << col1
				 << std::setw(wide - col1.size())
				 << col2
				 << "\n";

			//outFile(col1, col2);
			++leaves;
		};

		auto condition2 = [](int refValGlobal, int refValLocal) {
			return refValLocal < refValGlobal ? true : false;
		};

		auto ifCond2 = [&]() {
			foundAtDepth = depth;
			string col1 = fmt::format("B MVC found so far has {} elements", branchHandler.getRefValue());
			string col2 = fmt::format("B process {}, thread {}", branchHandler.getRankID(), id);
			cout << std::internal
				 << col1
				 << std::setw(wide - col1.size())
				 << col2
				 << "\n";

			//outFile(col1, col2);
			if (depth > measured_Depth)
			{
				measured_Depth = depth;
			}

			++leaves;
		};

		branchHandler.replace_refValGlobal_If<void>(solsize, condition1, ifCond1, cur_sol, user_serializer); // thread safe
		branchHandler.replace_refValGlobal_If<void>(solsize, condition2, ifCond2, cur_sol, user_serializer);


		return;
	}
    










































/*
	void mvc(int id, int depth, Graph &graph, void *parent)
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

		branchHandler.replace_refValGlobal_If<void>(graph.coverSize(), condition1, ifCond1, graph, user_serializer); // thread safe
		branchHandler.replace_refValGlobal_If<void>(graph.coverSize(), condition2, ifCond2, graph, user_serializer);

		return;
	}*/
};

#endif
