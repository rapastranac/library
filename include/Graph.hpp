#ifndef GRAPH_HPP
#define GRAPH_HPP

using namespace std;

#include <cereal/types/map.hpp>
#include <cereal/types/set.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/access.hpp>

#include "util.hpp"

#include <climits>
#include <cmath> /* floor, ceil */
#include <cassert>
#include <mutex>
#include <random>
#include <sstream>

class Graph
{
private:
	friend class cereal::access;

	struct FoldedVertices
	{
		int u{-1};
		int v{-1};
		int w{-1};
		/*this id is negative so it does not get confused with
			positive vertices' names, then negative vertices in graph
			will be those ones who were folded*/
		//int id;

		FoldedVertices()
		{
		}

		FoldedVertices(int u, int v, int w)
		{
			this->u = u;
			this->v = v;
			this->w = w;
		}

		template <class Archive>
		void serialize(Archive &ar, const unsigned int version)
		{
			ar(u, v, w);
		}
	};

	void _addRowToList(int vec0)
	{
		this->adj.insert(pair<int, set<int>>(vec0, rows));
		this->rows.clear();
	}

	void _calculerVertexMaxDegree()
	{
		int tmp;
		/*Finding vertex degrees, in order to start exploring by these ones.*/
		if (!vertexDegree.empty())
		{
			vertexDegree.clear();
			idsMax.clear();
			max = 0;
		}

		map<int, set<int>>::iterator it = this->adj.begin();
		while (it != adj.end())
		{
			tmp = it->second.size();
			this->vertexDegree.insert({it->first, tmp});
			if (tmp > this->max)
				this->max = tmp;
			++it;
		}

		it = adj.begin();
		while (it != adj.end())
		{
			if (this->vertexDegree[it->first] == this->max)
			{
				this->idsMax.push_back(it->first);
			}
			++it;
		}
	}

	void _calculerVertexMinDegree()
	{
		int tmp;
		/*Finding vertex degrees, in order to start exploring by these ones.*/
		if (!vertexDegree.empty())
		{
			idsMin.clear();
			min = numVertices;
		}

		map<int, set<int>>::iterator it = this->adj.begin();
		while (it != adj.end())
		{
			tmp = it->second.size();
			this->vertexDegree.insert({it->first, tmp});
			if (tmp < this->min)
				this->min = tmp;
			++it;
		}

		it = adj.begin();
		while (it != adj.end())
		{
			if (this->vertexDegree[it->first] == this->min)
			{
				this->idsMin.push_back(it->first);
			}
			++it;
		}
	}

	void _updateVertexDegree()
	{
		//Recalculating the vertex with maximum number of edges
		int max_tmp = 0;
		int min_tmp = numVertices;

		for (auto const &it : this->vertexDegree)
		{
			if (it.second > max_tmp)
			{
				max_tmp = it.second;
			}
			if (it.second < min_tmp)
			{
				min_tmp = it.second;
			}
		}
		this->max = max_tmp;
		this->min = min_tmp;
		this->idsMax.clear();
		this->idsMin.clear();
		/*storing position of highest degree vertices within adjacency list*/
		for (auto const &it : this->vertexDegree)
		{
			if (it.second == max)
				this->idsMax.push_back(it.first);
			if (it.second == min)
				this->idsMin.push_back(it.first);
		}
	}

	int _getRandomVertex(std::vector<int> &target)
	{
		/*Here this will explore the list of higest degree vertices and
			it will choose any of them randomly*/

		std::random_device rd;	// Will be used to obtain a seed for the random number engine
		std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()
		std::uniform_int_distribution<> distrib(0, target.size() - 1);

		int random = distrib(gen);
		return target[random];
	}

	void _readEdgesFromGraph()
	{

		int _counterEdges = 0;
		auto adj_cpy = this->adj;
		map<int, set<int>>::iterator it = adj_cpy.begin();

		while (it != adj_cpy.end())
		{
			_counterEdges += it->second.size();
			set<int>::iterator it2 = it->second.begin();
			while (it2 != it->second.end())
			{
				adj_cpy[*it2].erase(it->first);
				++it2;
			}
			++it;
		}
		this->numEdges = _counterEdges;
	}

	/*Preprocessing methods*/
	/*An isolated vertex (one of degree zero) cannot be in a vertex
	cover of optimal size. Because there are no edges incident upon
	such a vertex, there is no benefit in including it in any cover.
	Thus, in G0, an isolated vertex can be eliminated, reducing n0 by one.
	This rule is applied repeatedly until all isolated
	vertices are eliminated.*/
	bool _rule1(map<int, set<int>> &adj)
	{

		map<int, set<int>>::const_iterator it = adj.begin();

		std::vector<int> degree_zero;
		std::once_flag flag;
		bool isChanged = false;

		while (it != adj.end())
		{
			if (it->second.size() == 0)
			{
				degree_zero.push_back(it->first);
			}
			it++;
		}

		std::vector<int>::const_iterator _it_ = degree_zero.begin();
		while (_it_ != degree_zero.end())
		{

			adj.erase(*_it_);
			this->vertexDegree.erase(*_it_);
			numVertices--;

			_it_++;
			std::call_once(flag, [&isChanged]() {
				isChanged = true;
			});
		}

		return isChanged;
	}

	/*In the case of a pendant vertex (one of degree one), there is
	an optimal vertex cover that does not contain the pendant vertex
	but does contain its unique neighbor. Thus, in G�, both the pendant
	vertex and its neighbor can be eliminated. This also eliminates any
	additional edges incident on the neighbor, which may leave isolated
	vertices for deletion under Rule 1. This reduces n� by the number
	of deleted vertices and reduces k� by one. This rule is applied repeatedly
	until all pendant vertices are eliminated.*/
	bool _rule2(map<int, set<int>> &adj)
	{

		map<int, set<int>>::const_iterator it = adj.begin();
		set<int> pendant_neighbour;
		std::once_flag flag;
		bool isChanged = false;

		while (it != adj.end())
		{
			if (it->second.size() == 1)
			{
				int val = *it->second.begin();
				pendant_neighbour.insert(val);
				_cover.insert(val);
			}
			it++;
		}

		set<int>::const_iterator pn = pendant_neighbour.begin();

		//numEdges -= pendant_neighbour.size();
		while (pn != pendant_neighbour.end())
		{
			//numEdges -= adj[*pn].size();
			this->erase(adj, *pn);

			//this->vertexDegree.erase(*pn);
			//numVertices--;

			pn++;
			std::call_once(flag, [&isChanged]() {
				isChanged = true;
			});
		}

		return isChanged;
	}

	bool _rule3(map<int, set<int>> &adj)
	{
		/*		u ---- v ~~~
				 \	  /
				  \  /
				   w ~~~
		*/
		map<int, set<int>>::const_iterator u = adj.begin();
		bool isChanged = false;
		bool flag = false;
		std::once_flag oo_flag;

		while (u != adj.end())
		{
			if (u->second.size() == 2)
			{
				/*adjacent neighbours*/
				set<int>::const_iterator an = u->second.begin();
				int v = *an;
				an++;
				int w = *an;
				if (adj[v].contains(w) && adj[w].contains(v))
				{
					_cover.insert(v);
					_cover.insert(w);

					erase(adj, u->first);
					erase(adj, v);
					erase(adj, w);
					//this->numEdges -= 3;
					flag = true;
					std::call_once(oo_flag, [&isChanged]() {
						isChanged = true;
					});
				}
			}

			if (flag)
			{
				u = adj.begin();
				flag = false;
			}
			else
			{
				u++;
			}
		}
		return isChanged;
	}

	bool _rule4(map<int, set<int>> &adj)
	{
		/*		u ---- v ~~~
				 \				==>  ~~~(u')~~~
				  \
				   w ~~~
		*/

		map<int, set<int>>::const_iterator u = adj.begin();
		map<int, set<int>> folded_vertices;
		bool isChanged = false;
		std::once_flag oo_flag;

		int id = -1;

		bool flag = false;

		while (u != adj.end())
		{
			if (u->second.size() == 2)
			{
				/*adjacent neighbours*/
				set<int>::const_iterator an = u->second.begin();
				int v = *an;
				an++;
				int w = *an;
				if (!adj[v].contains(w) && !adj[w].contains(v))
				{
					//Check to not fold already folded vertices
					if (u->first < 0 || v < 0 || w < 0)
					{
						u++;
						continue;
					}

					//Create (u')
					FoldedVertices u_prime(u->first, v, w);
					//push to a adj of all the folded vertices
					foldedVertices.insert(pair<int, FoldedVertices>(id, u_prime));

					//Check neighbours of v
					set<int> foldedNeigbours;
					set<int>::const_iterator i = adj[v].begin();
					while (i != adj[v].end())
					{
						if (*i != u->first)
						{
							foldedNeigbours.insert(*i);
						}
						i++;
					}
					//Check neighbours of w
					i = adj[w].begin();
					while (i != adj[w].end())
					{
						if (*i != u->first)
						{
							foldedNeigbours.insert(*i);
						}
						i++;
					}

					//Erase u,v and w from the graph
					erase(adj, u->first);
					erase(adj, v);
					erase(adj, w);

					//Insert (u') into graph
					adj.insert(pair<int, set<int>>(id, foldedNeigbours));
					numVertices++;
					//link the neighbours of v and w to (u')
					i = foldedNeigbours.begin();
					while (i != foldedNeigbours.end())
					{
						adj[*i].insert(id);
						numEdges++;
						i++;
					}

					// post-processed graph has two fewer edges due to the folding
					//numEdges -= 2;

					id--;
					flag = true;

					std::call_once(oo_flag, [&isChanged]() {
						isChanged = true;
					});
				}
			}

			if (flag)
			{
				u = adj.begin();
				flag = false;
			}
			else
			{
				u++;
			}
		}
		return isChanged;
	}

	void erase(map<int, set<int>> &adj, int v)
	{
		try
		{
			set<int>::const_iterator it = adj[v].begin();

			while (it != adj[v].end())
			{
				adj[*it].erase(v);
				this->vertexDegree[*it]--;
				it++;
			}
			numEdges -= adj[v].size();
			numVertices--;
			adj.erase(v);
			this->vertexDegree.erase(v);
		}
		catch (const std::exception &e)
		{
			std::stringstream ss;
			ss << "Exception while erasing vertex from adj"
			   << '\n'
			   << e.what() << '\n';

			std::cerr << ss.str();
		}
	}

	void build(int n, double p)
	{
		int maxEdgesPossible = n * (n - 1) / 2;
		int maxEdgesPerNode = maxEdgesPossible / n;
		this->max = 0;
		this->min = 0;
		this->numEdges = 0;
		this->numVertices = 0;

		double _p = p * (double)maxEdgesPerNode / 100.0;

		int r = 0, m = 0;

		//srand(time(NULL)); //this commented to obtain always the same graph
		// Build the edges
		std::random_device rd;	// Will be used to obtain a seed for the random number engine
		std::mt19937 gen(rd()); // Standard mersenne_twister_engine seeded with rd()

		for (int i = 0; i < n; i++)
		{
			for (int j = i + 1; j < n; j++)
			{

				std::uniform_int_distribution<> distrib(1, n);
				r = distrib(gen);
				//r = rand() % n + 1;

				//if (r <= _p) {
				if (r <= p)
				{
					++m;
					adj[i].insert(j);
					adj[j].insert(i);
				}
			}
		}

		/*This second loop is used only if adj.size() != n*/
		// rand()%a + b => interval-> [b, b + a)
		if (adj.size() < n)
		{
			for (int i = 0; i < n; i++)
			{
				if (!adj.contains(i))
				{
					m++;
					int interval1 = i - 0;
					int interval2 = n - i;

					int low_bnd;
					int upp_bnd;
					int j;

					if (interval1 > interval2)
					{
						upp_bnd = i - 1;
						std::uniform_int_distribution<> distrib(0, upp_bnd);
						int tmp = distrib(gen);
						//int tmp = rand() % upp_bnd;
						j = tmp;
					}
					else
					{
						low_bnd = i + 1;
						upp_bnd = n - (i + 1);
						std::uniform_int_distribution<> distrib(low_bnd, upp_bnd);
						int tmp = distrib(gen);
						//int tmp = rand() % upp_bnd + low_bnd;
						j = tmp;
					}
					adj[i].insert(j);
					adj[j].insert(i);
					//if (adj.size() > n)
					//{
					//	int var = 5;
					//}
				}
				if (adj.size() == n)
					break;
			}
		}

		this->numEdges = m;
		this->numVertices = n;
		/*begin<----------4 testing purposes-------*/
		double mean = (double)m / n;
		double density = mean / maxEdgesPerNode;
	}

public:
	bool empty()
	{
		return _cover.empty();
	}
	//Default constructor
	Graph()
	{
		this->max = 0;
		this->min = 0;
		this->numEdges = 0;
		this->numVertices = 0;
	}

	//Parameterized constructor
	//N size, p propability out of 100
	Graph(int n, double p)
	{
		build(n, p);
		/*------------------------------------->end*/
		_calculerVertexMaxDegree();
		_calculerVertexMinDegree();
	}

	void addNeighbour(int val)
	{
		this->rows.insert(val);
	}

	void removeZeroVertexDegree()
	{
		try
		{
			/*This is also rule number 1 of preprocessing*/

			/*After erasing vertices, some of them might end up with zero degree,
			this function is in charge of erasing those vertices*/
			for (auto &i : _zeroVertexDegree)
			{
				adj.erase(i);
				vertexDegree.erase(i);
			}
			this->_zeroVertexDegree.clear();
			_updateVertexDegree();
		}
		catch (const std::exception &e)
		{
			std::stringstream ss;
			ss << "Exception while removing zero vertex degrees"
			   << '\n'
			   << e.what() << '\n';

			std::cerr << ss.str();
		}
	}

	void removeEdge(const int &v, const int &w)
	{
		adj[v].erase(w);
		adj[w].erase(v);

		vertexDegree[v]--;
		vertexDegree[w]--;

		if (adj[v].size() == 0)
			_zeroVertexDegree.insert(v);
		if (adj[w].size() == 0)
			_zeroVertexDegree.insert(w);

		numEdges--;
		_updateVertexDegree();
	}

	void removeVertex(int v)
	{
		try
		{
			if (!adj.contains(v))
			{
				throw "_VERTEX_NOT_FOUND";
			}

			numEdges = numEdges - adj[v].size();

			std::set<int>::const_iterator it = adj[v].begin();
			/*Here we explore all the neighbours of v, and then we find
			vertex v inside of those neighbours in order to erase v of them*/
			while (it != adj[v].end())
			{
				adj[*it].erase(v);
				if (adj[*it].size() == 0)
				{
					//Store temporary position of vertices that end up with no neighbours
					this->_zeroVertexDegree.insert(*it);
				}
				this->vertexDegree[*it]--;
				it++;
			}

			/*After v is been erased from its neighbours, then v is erased
			from graph and the VertexDegree is updated*/
			this->adj.erase(v);
			this->vertexDegree.erase(v);

			this->_cover.insert(v);
			numVertices--;
			_updateVertexDegree();
		}
		catch (const std::exception &e)
		{
			std::stringstream ss;
			ss << "Exception while removing vertex : "
			   << v
			   << '\n'
			   << e.what() << '\n';
			std::cerr << ss.str();
		}
	}

	// it returns the number of neighbours that were removed
	auto removeNv(int v)
	{
		std::set<int> neighboursOfv(adj[v]); //copy of neigbours of vertex v
		int nNeighours = neighboursOfv.size();
		for (auto i : neighboursOfv)
		{
			if (adj.contains(i))
			{
				this->_cover.insert(i);
				removeVertex(i);
			}
		}
		return nNeighours;
	}

	void readGraph(string NameOfFile, string directory)
	{
		using namespace std;
		string line;
		vector<string> split;
		int i = 0;
		int _counter_vertices = 0;

		while (1)
		{
			line = Util::GetFileLine(directory + NameOfFile, i);
			if (line == "")
				break;
			split = Util::Split(line, "\t");

			for (int i = 1; i != split.size(); i++)
			{
				addNeighbour(Util::ToInt(split[i]));
			}
			_addRowToList(Util::ToInt(split[0]));
			_counter_vertices++;

			i++;
		}
		_calculerVertexMaxDegree();
		_readEdgesFromGraph();
		//Graph::currentMVCSize = adj.size();
		this->numVertices = _counter_vertices;
	}

	void readEdges(string NameOfFile)
	{

		std::ifstream file(NameOfFile);

		if (!file.is_open())
		{
			printf("Input file not found");
			throw "Input file not found";
		}

		int u, v;
		int i = 0;
		while (!file.eof())
		{
			i++;
			file >> u >> v;
			adj[u].insert(v);
			adj[v].insert(u);
		}

		numEdges = i;
		numVertices = adj.size();

		/*4 testing*/
		double mean = (double)numEdges / (double)numVertices;
		double prob = mean * 100 / (double)numVertices;

		double maxEdgesPossible = numVertices * (numVertices - 1) / 2;
		double maxEdgesPerNode = maxEdgesPossible / (double)numVertices;
		double density = mean / maxEdgesPerNode;

		_calculerVertexMaxDegree();
		_calculerVertexMinDegree();
	}

	/*It explores the highest degree edges and choses whether
		the first one in the list or randomly*/
	int id_max(bool random = true)
	{
		return (random == true) ? _getRandomVertex(this->idsMax) : idsMax[0];
	}

	int id_min(bool random = true)
	{
		return (random == true) ? _getRandomVertex(this->idsMin) : idsMin[0];
	}

	int d_max()
	{
		return max;
	}

	int d_min()
	{
		return min;
	}

	//Returns graph's size
	int size()
	{
		return this->adj.size();
	}

	//returns cover size

	int coverSize()
	{
		return this->_cover.size();
	}

	//BETA:...
	bool isCovered()
	{
		return numEdges == 0 ? 1 : 0;
	}

	//gets neighbours of v, Nv(v) = {w1,w2, ... ,wi}
	const std::set<int> &operator[](const int v) const
	{
		map<int, set<int>>::const_iterator it = adj.find(v);

		if (it == adj.end())
		{
			throw "_VERTEX_NOT_FOUND";
		}
		else
		{
			return (it->second);
		}
	}

	typedef std::map<int, set<int>>::iterator iterator;

	iterator begin() { return adj.begin(); }
	iterator end() { return adj.end(); }

	size_t preprocessing()
	{

		clean_graph();
		auto _adj = this->adj;
		bool flag = true;
		flag = _rule4(_adj);
		this->adj = _adj;
		clean_graph();

		_readEdgesFromGraph();
		_calculerVertexMaxDegree();
		_calculerVertexMinDegree();

		this->numVertices = this->adj.size();

		return adj.size();
	}

	void clean_graph()
	{
		bool flag = true;
		bool flag2 = true;
		while (flag2)
		{
			if (flag2)
			{
				flag = true;
				while (flag)
				{
					flag = _rule1(this->adj);
					flag = _rule2(this->adj);
				}
			}

			flag2 = _rule3(this->adj);
		}

		this->_zeroVertexDegree.clear();
		_updateVertexDegree();
	}

	std::set<int> cover()
	{
		return _cover;
	}

	std::set<int> postProcessing()
	{
		//_cover.insert(-2);
		std::set<int>::iterator it = _cover.begin();
		set<int> unfolded_vertices;
		/*If a vertex is negative, it means it was folded, then we look up
			the foldedVertices to unfold it*/
		while (it != _cover.end())
		{
			/*It finds folded vertices (u'), which are negatives,
				if (u') is included in the cover, then, vertices
				u and v must be present in the cover*/
			if (*it < 0)
			{
				unfolded_vertices.insert(foldedVertices[*it].v);
				unfolded_vertices.insert(foldedVertices[*it].w);
				foldedVertices.erase(*it);
				_cover.erase(*it);
				it = _cover.begin();
			}
			else
				it++;
		}

		/* ******************************************************** */

		/* if (u') was not included in the cover, then u must be
			present in the cover */
		map<int, FoldedVertices>::const_iterator i = foldedVertices.begin();
		while (i != foldedVertices.end())
		{
			unfolded_vertices.insert(i->second.u);
			foldedVertices.erase(i->first);
			i = foldedVertices.begin();
		}

		/* ******************************************************** */

		/*Build minimum vertex cover*/
		set<int>::const_iterator j = unfolded_vertices.begin();

		while (j != unfolded_vertices.end())
		{
			this->_cover.insert(*j);
			j++;
		}
		return _cover;
	}

	int max_k()
	{
		if (!adj.empty())
		{
			//double mxDegrees = list[idsMax[0]].size();
			//return floor((double)list.size() / (1.0 + (1.0 / (mxDegrees))));
			return ceil((double)adj.size() / (1.0 + (1.0 / (max))));
		}
		return 0;
	}

	int min_k()
	{
		if (!adj.empty())
		{
			//	double mxDegrees = list[idsMax[0]].size();
			//	double minDegrees = list[idsMin[0]].size();
			//return floor((double)list.size() / (1.0 + (mxDegrees / minDegrees)));
			return floor((double)adj.size() / (1.0 + ((double)max / (double)min)));
		}
		return 0;
	}

	int getNumEdges()
	{
		return this->numEdges;
	}

	void build_graph(int SIZE, int p)
	{

		build(SIZE, p);
	}

	void print_edges(std::ofstream &file)
	{
		map<int, set<int>>::const_iterator it = adj.begin();

		/*Fix this to not printing duplicated edges*/

		while (!adj.empty())
		{
			set<int>::const_iterator it2 = (*it).second.begin();
			while (!(*it).second.empty())
			{
				file << (*it).first << "\t" << *it2;
				int v = (*it).first;
				int w = *it2;
				removeEdge(v, w);

				if (!(*it).second.empty())
					file << endl;

				it2 = (*it).second.begin();
			}
			removeZeroVertexDegree();
			if (!adj.empty())
				file << endl;

			it = adj.begin();
		}
	}

	int find_vi(std::vector<int> &vi)
	{
		auto adj_cpy = adj;
		size_t sum = 0;
		size_t MAX = max;
		size_t edges_p = numEdges;

		auto findMax = [&adj_cpy]() {
			int max = 0;

			for (auto &[key, val] : adj_cpy)
			{
				if (val.size() > max)
					max = val.size();
			}
			return max;
		};

		auto iterator = adj_cpy.begin();
		while (iterator != adj_cpy.end())
		{
			int key = iterator->first;
			auto &neigbours = iterator->second;
			if (neigbours.size() == MAX)
			{
				vi.push_back(key);
				sum += neigbours.size();

				edges_p -= neigbours.size();

				for (auto &neighbour : neigbours)
				{
					adj_cpy[neighbour].erase(key);
				}
				adj_cpy.erase(key);
				MAX = findMax();

				if (sum >= numEdges)
					break;

				iterator = adj_cpy.begin();
			}
			else
				iterator++;
		}

		return 0;
	}
	int DegLB()
	{
		if (adj.size() == 0)
			return 0;

		std::vector<int> vi;
		int sum = find_vi(vi);
		int last = vi.back();
		int deg_last = adj[last].size();

		int i = vi.size();

		//int fraction = floor((double)numEdges / (double)deg_last);

		return i;
	}

	/*		TEMPORARY		*/

	std::vector<std::vector<int>> ADJ_MATRIX()
	{

		int N = adj.size();
		std::vector<std::vector<int>> tmp(N, std::vector<int>(N, 0));

		map<int, set<int>>::const_iterator it = adj.begin();
		while (it != adj.end())
		{
			set<int>::const_iterator jt = it->second.begin();
			while (jt != it->second.end())
			{
				tmp[it->first][*jt] = 1;
				jt++;
			}
			it++;
		}
		return tmp;
	}

	std::vector<int> DEGREE()
	{
		std::vector<int> tmp;

		std::map<int, set<int>>::const_iterator it = adj.cbegin();

		while (it != adj.cend())
		{
			tmp.push_back(it->second.size());
			it++;
		}
		return tmp;
	}

	//Graph(const Graph &) = default;
	//Graph(Graph &&) = default;
	//Graph &operator=(const Graph &) = default;
	//Graph &operator=(Graph &&) = default;
	//virtual ~Graph() = default;

	template <class Archive>
	void serialize(Archive &ar)
	{
		ar(max,
		   min,
		   idsMax,
		   idsMin,
		   adj,
		   rows,
		   vertexDegree,
		   _zeroVertexDegree,
		   foldedVertices,
		   _cover,
		   numEdges,
		   numVertices);
	}

private:
	int max;						  /*Highest degree within graph*/
	int min;						  /*Lowest degree within graph*/
	std::vector<int> idsMax;		  /*Stores the positions of max degree
									vertices within the adjacency adj*/
	std::vector<int> idsMin;		  /*same as above but for min degree*/
	std::map<int, std::set<int>> adj; /*Adjacency list*/
	std::set<int> rows;				  /*Temporary variable to store*/
	std::map<int, int> vertexDegree;  /*list of vertices with their corresponding
									number of edges*/
	std::set<int> _zeroVertexDegree;  /*List of vertices with zero degree*/

	std::map<int, FoldedVertices> foldedVertices;
	std::set<int> _cover;

	int numEdges;	 //number of edges
	int numVertices; //number of vertices
};

#endif