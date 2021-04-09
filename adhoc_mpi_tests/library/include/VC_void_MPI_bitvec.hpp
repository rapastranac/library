#ifdef VC_VOID_MPI


#include <mpi.h>

#include <boost/dynamic_bitset.hpp>
#include <boost/container/set.hpp>
#include <boost/unordered_set.hpp>

#include <deque>

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


class VC_MPI;


#define TAG_TASK 1
#define TAG_NOWORK 2
#define TAG_TERMINATE 3
#define TAG_AVAIL 4
#define TAG_OPTSOL 5
#define TAG_STARTEDWORKING 6

#define STATE_WORKING 1
#define STATE_ASSIGNED 2
#define STATE_AVAIL 3

class VCScheduler
{
public:
	int bestVal;
	bool isdone;
	int world_size;
	int world_rank;
	VC_MPI* vcmpi;
	deque<std::pair<gbitset, gbitset>> tasks;
	
	MPI_Win winBestSol;
	int* winBestSolBuf;
	
	
	MPI_Win winJobTakers;
	int* winJobTakersBuf;
	
	int getBestVal();
	
	
	void setBestVal(int bval);
	
	void addTask(gbitset& bits_in_graph, gbitset& cursol);
	

	void callLatestTask();
	
	void setDone();
	
	
	vector<int> states;
	
	VCScheduler()
	{
		
	}
	
	
	~VCScheduler()
	{
		cout<<"DESTROY SCHED "<<world_rank<<endl;
	}
	
	void initMPI()
	{
		

		// Get the number of processes
		MPI_Comm_size(MPI_COMM_WORLD, &world_size);

		// Get the rank of the process
		MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

		// Get the name of the processor
		char processor_name[MPI_MAX_PROCESSOR_NAME];
		int name_len;
		MPI_Get_processor_name(processor_name, &name_len);

		
		if (world_rank == 0)
		{
			for (int i = 0; i < world_size; ++i)
			{
				states.push_back(STATE_WORKING);	//by default, everyne works, unless they tell us otherwise
			
			}
		}
		
		
		MPI_Alloc_mem(1 * sizeof(int), MPI_INFO_NULL, &winBestSolBuf);
		MPI_Win_create(winBestSolBuf, sizeof(int), sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &winBestSol);
		MPI_Win_fence(0, winBestSol);
		
		
		
		
		MPI_Alloc_mem(world_size * sizeof(int), MPI_INFO_NULL, &winJobTakersBuf);
		for (int i = 0; i < world_size; i++)
		{
			winJobTakersBuf[i] = 0;
		}
		MPI_Win_create(winJobTakersBuf, sizeof(int) * world_size, sizeof(int), MPI_INFO_NULL, MPI_COMM_WORLD, &winJobTakers);
		MPI_Win_fence(0, winJobTakers);
		
		

		// Print off a hello world message
		printf("Init done : processor %s, rank %d out of %d processors\n",
		processor_name, world_rank, world_size);
	
	}
	
	void finalize()
	{

		MPI_Win_free(&winBestSol);
		
		MPI_Win_free(&winJobTakers);
		
		MPI_Free_mem(winJobTakersBuf);
		MPI_Free_mem(winBestSolBuf);
		// Finalize the MPI environment.
		
	
	}
	
	
	
	
	
	void checkTaskSending()
	{
		MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, winJobTakers);
		for (int i = 1; i < world_size; i++)
		{
			
			if (winJobTakersBuf[i] == 1)
			{
				winJobTakersBuf[i] = 0;
				if (tasks.empty())
				{
					
					MPI_Send(&world_rank, 1, MPI_INT, i, TAG_NOWORK, MPI_COMM_WORLD);
				}
				else
				{
					std::pair<gbitset, gbitset>& task = tasks.front();
					sendTaskTo(task, i);	
					tasks.pop_front();
				}
			}
		}
		MPI_Win_unlock(world_rank, winJobTakers);
	}
	
	
	
	
	void sendTaskTo(std::pair<gbitset, gbitset>& task, int dest)
	{
		/*cout<<"Rank "<<world_rank<<" sending task to "<<dest<<endl;
		vector<int> ints;

		ints.reserve(task.first.num_blocks());
		boost::to_block_range(task.first, std::back_inserter(ints));
		
		for (int i : ints)
			cout<<i<<endl;
		
		MPI_Send(ints.data(), ints.size(), MPI_INT, dest, TAG_TASK, MPI_COMM_WORLD);
		
		
		vector<int> ints2;

		ints2.reserve(task.second.num_blocks());
		boost::to_block_range(task.second, std::back_inserter(ints2));
		
		
		MPI_Send(ints2.data(), ints2.size(), MPI_INT, dest, TAG_TASK, MPI_COMM_WORLD);*/
	
	
		/*cout<<"Rank "<<world_rank<<" sending task to "<<dest<<endl;
		cout<<task.first<<endl<<task.second<<endl;*/
		
	
		vector<int> ints;

		ints.reserve(task.first.size());
		
		for (int i = 0; i < task.first.size(); ++i)
		{
			ints.push_back(task.first[i] ? 1 : 0);
		}
		
		MPI_Send(ints.data(), ints.size(), MPI_INT, dest, TAG_TASK, MPI_COMM_WORLD);
		
		
		vector<int> ints2;

		ints2.reserve(task.second.size());
		
		for (int i = 0; i < task.second.size(); ++i)
		{
			ints2.push_back(task.second[i] ? 1 : 0);
		}
		
		MPI_Send(ints2.data(), ints2.size(), MPI_INT, dest, TAG_TASK, MPI_COMM_WORLD);
	
	
	}
	
	
	
	void runNode()
	{
		
		while (true)
		{
			while (!tasks.empty())
			{
				callLatestTask();
				
				/*cout<<"winJobTakersBuf = ";
				for (int i = 0; i < world_size; i++)
				{
					cout<<winJobTakersBuf[i];
				}
				cout<<endl;*/
			}
			
			MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_AVAIL, MPI_COMM_WORLD);
			
			
			MPI_Status status;
			int number_amount;

			MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			MPI_Get_count(&status, MPI_INT, &number_amount);

			int* number_buf = (int*)malloc(sizeof(int) * number_amount);

			MPI_Recv(number_buf, number_amount, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			if (status.MPI_TAG == TAG_TASK)
			{
				//cout<<"Rank "<<world_rank<<" received task"<<endl;
				gbitset ingraph(number_amount);
				for (int i = 0; i < number_amount; ++i)
				{
					ingraph[i] = (number_buf[i] == 1 ? true : false);
				}
				
				//second param = cursol
				MPI_Recv(number_buf, number_amount, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
				gbitset cursol(number_amount);
				for (int i = 0; i < number_amount; ++i)
				{
					cursol[i] = (number_buf[i] == 1 ? true : false);
				}
			
				if (ingraph.size() > 20)
				{
					MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_STARTEDWORKING, MPI_COMM_WORLD);
				}	
				//cout<<"********* the task ***********"<<endl<<ingraph<<endl<<cursol<<endl<<"**********************************"<<endl;
				
				addTask(ingraph, cursol);
			
			}
			else if (status.MPI_TAG == TAG_NOWORK)
			{
				MPI_Send(&world_rank, 1, MPI_INT, 0, TAG_AVAIL, MPI_COMM_WORLD);
			}
			else if (status.MPI_TAG == TAG_TERMINATE)
			{
				cout<<"Rank "<<world_rank<<" got a terminate call"<<endl;
				free(number_buf);
				break;
			}
			
			free(number_buf);
				
		
		}
	
	}
	
	
	
	void runCenter()
	{
		while (true)
		{
			MPI_Status status;
			int number_amount;

			MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &status);
			MPI_Get_count(&status, MPI_INT, &number_amount);

			int* number_buf = (int*)malloc(sizeof(int) * number_amount);

			MPI_Recv(number_buf, number_amount, MPI_INT, status.MPI_SOURCE, status.MPI_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

			
			if (status.MPI_TAG == TAG_AVAIL)
			{
				//cout<<"CENTER : now available:  "<<status.MPI_SOURCE<<endl;
				states[status.MPI_SOURCE] = STATE_AVAIL;
			}
			else if (status.MPI_TAG == TAG_OPTSOL)
			{
				cout<<"CENTER : updating bestVal to "<<number_buf[0]<<endl;
				if (number_buf[0] < this->bestVal)
				{
					bestVal = number_buf[0];
				
					for (int i = 1; i < world_size; ++i)
					{
				        MPI_Win_lock(MPI_LOCK_EXCLUSIVE, i, 0, winBestSol);
						MPI_Put(&bestVal, 1, MPI_INT, i, 0, 1, MPI_INT, winBestSol);
						MPI_Win_unlock(i, winBestSol);
				    }
				        //MPI_Win_fence(0, winBestSol);
				}
			}
			else if (status.MPI_TAG == TAG_STARTEDWORKING)
			{
				states[status.MPI_SOURCE] = STATE_WORKING;
			}
			
			
			bool hasWorking = false;
			vector<int> avail;
			vector<int> working;
			for (int i = 1; i < world_size; ++i)
			{
				if (states[i] == STATE_WORKING)
				{
					hasWorking = true;
					working.push_back(i);
				}
				else if (states[i] == STATE_AVAIL)
				{
					avail.push_back(i);
				}
			}
			
			
			
			
			if (hasWorking)
			{
				
				int cptworking = 0;
				for (int i : avail)	//assign all avail
				{
					int one = 1;
					//cout<<"Putting one to "<< working[cptworking] << " at index "<<i<<endl;
					MPI_Win_lock(MPI_LOCK_EXCLUSIVE, working[cptworking], 0, winJobTakers);
					MPI_Put(&one, 1, MPI_INT, working[cptworking], i, 1, MPI_INT, winJobTakers); 
					MPI_Win_unlock(working[cptworking], winJobTakers);
					
					
					cptworking = (cptworking + 1) % working.size();
					states[i] = STATE_ASSIGNED;
				}
				
			}
			
			
			
			
			
			if (hasWorking == false)
			{
				
				for (int i = 1; i < world_size; ++i)
				{
					MPI_Send(&world_rank, 1, MPI_INT, i, TAG_TERMINATE, MPI_COMM_WORLD);
				}
				free(number_buf);
				break;
			}
			
			free(number_buf);
		
		}
	
	}
	

};




class VC_MPI
{
	
private:
    	VCScheduler& scheduler;
public:

	    vector<boost::unordered_set<pair<gbitset,int>>> seen;
	    long is_skips;
	    long deglb_skips;
	    long seen_skips;
	    
	    unordered_map<int, gbitset> graphbits;
	    long passes;

	VC_MPI(VCScheduler &sch) :  scheduler(sch)
	{
		
	}
	~VC_MPI() {scheduler.vcmpi = nullptr;}




	void setGraph(Graph &graph)
	{
		is_skips = 0;
    		deglb_skips = 0;
    		seen_skips = 0;

		
			
		
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

		cout<<"Graph bits loaded"<<endl;

	}
	
	
	
	
	
	void mvcbitset(gbitset bits_in_graph, gbitset cur_sol)
	{
		passes++;
		
		
		scheduler.checkTaskSending();
		
		
		/*cout<<bits_in_graph.size()<<endl;
		cout<<cur_sol.size()<<endl;
		cout<<graphbits.size()<<endl;
		cout<<graphbits[0].size()<<endl;*/
		
		int cursol_size = cur_sol.count();
		
		
		
		if (passes % 10000000 == 0)
		{
			cout<<"wr="<<scheduler.world_rank<<" passes="<<passes<<" gsize="<<bits_in_graph.count()<<" refvalue="<<scheduler.getBestVal()<<" solsize="<<cur_sol.count()<<" isskips="<<is_skips<<" deglbskips="<<deglb_skips<<" seen_skips="<<seen_skips<<" seen.size="<<seen.size()<<endl;
			//cout<<"ID="<<id<<" CSOL="<<cursol_size<<" REFVAL="<<branchHandler.getRefValue()<<endl;
			
		}
		
		

		//cout<<"depth="<<depth<<" ref="<<branchHandler.getRefValue()<<"cursolsize="<<cursol_size<<" cnt="<<bits_in_graph.count()<<" sol="<<cur_sol<<endl;
		if (bits_in_graph.count() <= 1)
		{
			
			if (cursol_size < scheduler.getBestVal())
			{
				scheduler.setBestVal(cursol_size);
			}
			scheduler.setDone();
			return;
		}

		
		if (cursol_size >= scheduler.getBestVal())
		{
			scheduler.setDone();
			return;
		}
		
		
		/*if (bits_in_graph.count() <= 90 && bits_in_graph.count() >= 120 )
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
		}*/
			
		
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
			if (cursol_size < scheduler.getBestVal())
			{
				scheduler.setBestVal(cursol_size);
			}
			scheduler.setDone();
			return;
		}
		
		
		float tmp = (float)(1 - 8 * nbEdgesDoubleCounted/2 - 4*nbVertices + 4*nbVertices*nbVertices);
		int indsetub = (int)(0.5f * (1.0f + sqrt(tmp)));
		int vclb = nbVertices - indsetub;

		if (vclb + cursol_size >= scheduler.getBestVal())
		{
			is_skips++;
			scheduler.setDone();
			return;
		}
		
		
		
		int degLB = 0;//getDegLB(bits_in_graph, nbEdgesDoubleCounted/2);
		degLB = (nbEdgesDoubleCounted/2)/maxdeg;

		if (degLB + cursol_size >= scheduler.getBestVal())
		{
			deglb_skips++;
			scheduler.setDone();
			return;
		}
		

		/*if (maxdeg <= 2)
		{
			//TODO : ACTUALLY COMPUTE THE CYCLES
			terminate_condition_bits(cur_sol, cursol_size + nbVertices/2, id, depth);
			//cout<<"MAXDEG 2 n="<<nbVertices<<endl;
			return;
		}*/

		
		
		//right branch = take out v nbrs
		gbitset ingraph2 = bits_in_graph;

		ingraph2 = bits_in_graph & (~graphbits[maxdeg_v]);
		gbitset nbrs = (graphbits[maxdeg_v] & bits_in_graph);
		gbitset sol2 = cur_sol | nbrs;	//add all nbrs to solution
		int solsize2 = cursol_size + nbrs.count();
		
		if (solsize2 < scheduler.getBestVal())
		{
			scheduler.addTask(ingraph2, sol2);
		}
		
		
		
		gbitset ingraph1 = bits_in_graph;
		ingraph1.set(maxdeg_v, false);
		gbitset sol1 = cur_sol;
		sol1.set(maxdeg_v, true);
		int solsize1 = cursol_size + 1;
		
		if (solsize1 <= scheduler.getBestVal())
			mvcbitset(ingraph1, sol1);
		
		
		
		

		scheduler.setDone();
	
	
	} 

    
};





	





	
int VCScheduler::getBestVal()
{
	MPI_Win_lock(MPI_LOCK_EXCLUSIVE, world_rank, 0, winBestSol);
	if (winBestSolBuf[0] < bestVal)
	{
		bestVal = winBestSolBuf[0];
	}
	MPI_Win_unlock(world_rank, winBestSol);
	return bestVal;
}


void VCScheduler::setBestVal(int bval)
{
	
	if (bval < bestVal)
	{
		cout<<"PROC "<<world_rank<<" found a solution of size "<<bval<<", sending"<<endl;
		bestVal = bval;
		MPI_Send(&bval, 1, MPI_INT, 0, TAG_OPTSOL, MPI_COMM_WORLD);
	}
}

void VCScheduler::addTask(gbitset& bits_in_graph, gbitset& cursol)
{
	//int nbv = bits_in_graph.count();
	
	//tasksBySize[nbv].push_back( std::make_pair(bits_in_graph, cursol) );

	//cout<<"added task "<<bits_in_graph<<endl;
	
	tasks.push_back( std::make_pair(bits_in_graph, cursol) );
}


void VCScheduler::callLatestTask()
{
	std::pair<gbitset, gbitset> t = tasks.back();
	tasks.pop_back();
	
	//cout<<"calling task "<<t.first<<endl;
	vcmpi->mvcbitset(t.first, t.second);
	
}



void VCScheduler::setDone()
{
	//isdone = true;
}
	
	
	
	











class VCCenter
{



};











#endif
