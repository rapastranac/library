#ifndef VERTEXCOVER_HPP
#define VERTEXCOVER_HPP

#include <cereal/archives/binary.hpp>

#include "Graph.hpp"
#include "BranchHandler.hpp"
#include "ResultHolder.hpp"
#include "util.hpp"
#include "fmt/core.h"

#include <filesystem>
#include <iomanip>
#include <vector>

using namespace std::placeholders;

namespace fs = std::filesystem;

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

class VertexCover
{
public:
	VertexCover()
	{
		//this->_f = std::bind(&VertexCover::mvc, this, _1, _2, _3, _4);
		this->_f = std::bind(&VertexCover::mvc, this, _1, _2, _3);
	}
	virtual ~VertexCover() = default;

	void init(Graph &graph, int numThreads, std::string file, int prob)
	{
		this->graph = graph;
		input_file_name = file;
		this->numThreads = numThreads;
		std::string graph_size = std::to_string(graph.size());
		std::string nameout = graph_size + "_out.dat";
		std::string nameout_raw = graph_size + "_out_raw.csv";

		std::string dir = graph_size;
		std::string threads_dir = std::to_string(numThreads);

		this->outPath = "output/";

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath += "prob_" + std::to_string(prob) + "/";

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath += dir + "/";

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath += threads_dir;

		if (!fs::is_directory(outPath))
		{
			fs::create_directory(outPath);
		}

		this->outPath = outPath + "/";
		this->outPath_raw = outPath;

		this->outPath = outPath + nameout;
		this->outPath_raw = outPath_raw + nameout_raw;

		this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
		this->output_raw.open(outPath_raw, std::ofstream::in | std::ofstream::out | std::ofstream::app);
		if (!output.is_open())
		{
			printf("Error, output file not found !");
		}
		output.close();
		output_raw.close();
	}

	bool findCover(size_t run)
	{
		string msg_center = fmt::format("run # {} ", run);
		msg_center = "!" + fmt::format("{:-^{}}", msg_center, wide - 2) + "!" + "\n";
		cout << msg_center;
		outFile(msg_center, "");

		//this->branchHandler.setStrategy(0, 0);
		this->branchHandler.setMaxThreads(numThreads);
		this->branchHandler.functionIsVoid();

		//branchHandler.wrap(mvc2);			//strategy 1,2
		//branchHandler.setMaxDepth(4);		//strategy 1,2

		//size_t _k_mm = maximum_matching(graph);
		//size_t _k_uBound = graph.max_k();
		//size_t _k_lBound = graph.min_k();
		//size_t k_prime = std::min(k_mm, k_uBound);
		//currentMVCSize = k_prime;
		//preSize = graph.size();
		preSize = graph.preprocessing();
		//graph.postProcessing();

		size_t k_mm = maximum_matching(graph);
		size_t k_uBound = graph.max_k();
		size_t k_lBound = graph.min_k();
		size_t k_prime = std::min(k_mm, k_uBound) + graph.coverSize();
		currentMVCSize = k_prime;

		//k_prime = 83;

		//graph.currentMVCSize = k_prime;

		begin = std::chrono::steady_clock::now();

		try
		{
			branchHandler.setRefValue(&currentMVCSize);
			mvc(-1, 0, graph);
			branchHandler.waitResult(true);
			graph_res = branchHandler.retrieveResult<Graph>();
			cover = graph_res.postProcessing();
		}
		catch (std::exception &e)
		{
			this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
			if (!output.is_open())
			{
				printf("Error, output file not found !");
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

	void outFile(std::string col1, std::string col2)
	{
		//std::unique_lock<std::mutex> lck(mtx);
		this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
		if (!output.is_open())
		{
			printf("Error, output file not found !");
		}

		output << std::internal
			   << col1
			   << std::setw(wide - col1.size())
			   << col2
			   << "\n";

		output.close();
	}

	void printSolution()
	{
		this->output.open(outPath, std::ofstream::in | std::ofstream::out | std::ofstream::app);
		if (!output.is_open())
		{
			printf("Error, output file not found !");
		}

		cout << "!" << fmt::format("{:-^{}}", "Minimum vertex cover", wide - 2) << "!"
			 << "\n";
		output << "!" << fmt::format("{:-^{}}", "Minimum vertex cover", wide - 2) << "!"
			   << "\n";

		/* create string of lenght wide*/
		auto it = cover.begin();
		string str;
		while (it != cover.end())
		{
			str += Util::ToString(*it) + "   ";
			if (str.size() >= wide)
			{
				std::cout << fmt::format("{:<{}}", str, wide);
				std::cout << "\n";
				output << fmt::format("{:<{}}", str, wide);
				output << "\n";
				str.clear();
			}
			it++;
		}
		cout << fmt::format("{:<{}}", str, wide);
		cout << "\n";
		output << fmt::format("{:<{}}", str, wide);
		output << "\n";
		str.clear();
		cout << fmt::format("{:-^{}}", "", wide) << "\n";
		output << fmt::format("{:-^{}}", "", wide) << "\n";

		cout << "\n";
		output << "\n";

		string col1 = "path";
		string col2 = input_file_name;

		cout << std::internal
			 << col1
			 << std::setfill(' ')
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Initial graph size after preprocessing: ";
		col2 = Util::ToString((int)preSize);
		cout << std::internal
			 << col1
			 << std::setfill(' ')
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Size:";
		col2 = Util::ToString((int)cover.size());
		cout << std::internal
			 << col1
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Found at depth:";
		col2 = Util::ToString((int)foundAtDepth);
		cout << std::internal
			 << col1
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Elapsed time:";
		col2 = Util::ToString((double)(elapsed_secs * 1.0e-9)) + " s";
		//auto tmp = std::setw(wide - col1.size() - col2.size());
		cout << std::internal
			 << col1
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Number of leaves:";
		col2 = Util::ToString((int)leaves);
		cout << std::internal
			 << col1
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Maximum depth reached:";
		col2 = Util::ToString((int)measured_Depth);
		cout << std::internal
			 << col1
			 << std::setw(wide - col1.size()) << col2
			 << "\n";
		output << std::internal
			   << col1
			   << std::setw(wide - col1.size()) << col2
			   << "\n";

		col1 = "Idle time:";
		col2 = Util::ToString((double)(branchHandler.getIdleTime() * 1.0e-9));
		string col3 = Util::ToString((double)(branchHandler.getIdleTime() * 100.0 / elapsed_secs)) + "%";

		cout << std::left << std::setw(wide * 0.3)
			 << col1
			 << std::right << std::setw(wide * 0.3)
			 << col2
			 << std::right << std::setw(wide * 0.4)
			 << col3
			 << "\n";
		output << std::left << std::setw(wide * 0.3)
			   << col1
			   << std::right << std::setw(wide * 0.3)
			   << col2
			   << std::right << std::setw(wide * 0.4)
			   << col3
			   << "\n";

		col1 = "Pool idle time:";
		col2 = Util::ToString((double)(branchHandler.getPoolIdleTime()));
		col3 = Util::ToString((double)(branchHandler.getPoolIdleTime() * 100.0 / elapsed_secs)) + "%";

		cout << std::left << std::setw(wide * 0.3)
			 << col1
			 << std::right << std::setw(wide * 0.3)
			 << col2
			 << std::right << std::setw(wide * 0.4)
			 << col3
			 << "\n";
		output << std::left << std::setw(wide * 0.3)
			   << col1
			   << std::right << std::setw(wide * 0.3)
			   << col2
			   << std::right << std::setw(wide * 0.4)
			   << col3
			   << "\n";

		std::cout << "!" << fmt::format("{:-^{}}", "", wide - 2) << "!"
				  << "\n";
		output << "!" << fmt::format("{:-^{}}", "", wide - 2) << "!"
			   << "\n";
		std::cout << "\n"
				  << "\n"
				  << "\n";
		output << "\n"
			   << "\n"
			   << "\n";

		output.close();

		this->output_raw.open(outPath_raw, std::ofstream::in | std::ofstream::out | std::ofstream::app);

		output_raw << input_file_name << ","
				   << Util::ToString((int)preSize) << ","
				   << Util::ToString((int)cover.size()) << ","
				   << Util::ToString((int)foundAtDepth) << ","
				   << Util::ToString((double)(elapsed_secs * 1.0e-9)) << ","
				   << Util::ToString((int)leaves) << ","
				   << Util::ToString((int)measured_Depth) << ","
				   << Util::ToString((double)(branchHandler.getIdleTime() * 1.0e-9)) << ","
				   << Util::ToString((double)(branchHandler.getPoolIdleTime())) << "\n";
		output_raw.close();
	}

	using HolderType = library::ResultHolder<void, int, Graph>;

	void mvc(int id, int depth, Graph &graph)
	//void mvc(int id, int depth, Graph &graph, HANDLER::ResultHolder *parent)
	{
		size_t k1 = graph.min_k();
		size_t k2 = graph.max_k();
		size_t k = relaxation(k1, k2);

		if (k + graph.coverSize() >= currentMVCSize)
		{
			size_t addition = k + graph.coverSize();
			//printf("%d + %d = %d \n", k, visited.size(), k + visited.size());
			return;
		}

		if (graph.size() == 0)
		{
			terminate_condition(graph, id, depth);
			return;
		}

		Graph gLeft = graph;			 /*Let gLeft be a copy of graph*/
		Graph gRight = std::move(graph); // graph;	/*Let gRight be a copy of graph*/
		int newDepth = depth + 1;

		int v = gLeft.id_max(false);

		HolderType hol_l(branchHandler);
		HolderType hol_r(branchHandler);

		gLeft.removeVertex(v); //perform deletion before checking if worth to explore branch
		gLeft.clean_graph();
		size_t C1Size = gLeft.coverSize();

		if (C1Size < currentMVCSize)
		{
			hol_l.holdArgs(newDepth, gLeft);
			branchHandler.push(_f, id, hol_l);
		}

		gRight.removeNv(v);
		gRight.clean_graph();
		//gRight.removeZeroVertexDegree();

		size_t C2Size = gRight.coverSize();
		hol_r.holdArgs(newDepth, gRight);

		if (C2Size < currentMVCSize || hol_r.isBound())
		{
			branchHandler.forward(_f, id, hol_r);
		}
		return;
	}

private:
	const size_t relaxation(const size_t &k1, const size_t &k2)
	{
		return floor(((1.0 - factor) * (double)k1 + factor * (double)k2));
	}

	//const std::vector<int>& terminate_condition(std::vector<int>& visited, int id, int depth)
	void
	terminate_condition(Graph &graph, int id, int depth)
	{
		auto condition1 = [this](int, int) {
			return leaves == 0 ? true : false;
		};

		auto condition2 = [](int refValGlobal, int refValLocal) {
			return refValLocal < refValGlobal ? true : false;
		};

		bool cond1 = branchHandler.replaceIf(graph.coverSize(), condition1, graph); // thread/process safe
		bool cond2 = branchHandler.replaceIf(graph.coverSize(), condition2, graph);

		if (cond1)
		{
			//currentMVCSize = graph.coverSize();
			foundAtDepth = depth;
			string col1 = fmt::format("MVC found so far has {} elements", currentMVCSize);
			string col2 = fmt::format("thread {}", id);
			cout << std::internal
				 << std::setfill('.')
				 << col1
				 << std::setw(wide - col1.size())
				 << col2
				 << "\n";

			outFile(col1, col2);
			branchHandler.catchBestResult(graph); //to be checked!!
		}
		else if (cond2)
		{
			//currentMVCSize = graph.coverSize();
			foundAtDepth = depth;
			string col1 = fmt::format("MVC found so far has {} elements", currentMVCSize);
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
			branchHandler.catchBestResult(graph); //to be checked!!
		}
		leaves++;

		return;
	}

	const std::vector<int> &returnRes(std::vector<int> &VC1, std::vector<int> &VC2)
	{
		if (!VC1.empty() && !VC2.empty())
		{
			if (VC1.size() >= VC2.size())
				return VC2;
			else
				return VC1;
		}
		else if (!VC1.empty() && VC2.empty())
			return VC1;
		else if (VC1.empty() && !VC2.empty())
			return VC2;
		else
			return std::vector<int>();
	}

	size_t maximum_matching(Graph g)
	{
		size_t k = 0;
		size_t v, w;

		while (!g.isCovered())
		{
			v = g.id_max(false);
			w = *g[v].begin();
			k += 2;
			try
			{
				g.removeVertex(v);
				g.removeVertex(w);
			}
			catch (std::string e)
			{
			}
		}
		return k;
	}

private:
	library::BranchHandler &branchHandler = library::BranchHandler::getInstance();

	//std::function <std::vector<int>(int, int, Graph&, std::vector<int>&, Holder*)> _f;
	//std::function <void(int, int, Graph&, std::vector<int>&, Holder*)> _f;

	//std::function<void(int, int, Graph &, HANDLER::ResultHolder *)> _f;
	std::function<void(int, int, Graph &)> _f;
	Graph graph;
	Graph graph_res;
	std::chrono::steady_clock::time_point begin;
	std::chrono::steady_clock::time_point end;
	double elapsed_secs;
	size_t preSize = 0;
	std::mutex mtx;

	std::set<int> cover;
	std::vector<int> visited;

	size_t leaves = 0;
	int currentMVCSize = 0;
	size_t refGlobal = 0;
	size_t foundAtDepth = 0;
	size_t measured_Depth = 0;

	double factor = 0.0; /* k_lower_bound [0.0 - 1.0] k_upper_bound*/
	ofstream output;
	ofstream output_raw;
	std::string outPath;
	std::string outPath_raw;
	int wide = 60;

	std::string input_file_name;
	int numThreads = 0;
};

#endif