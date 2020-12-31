#include "../include/main.h"
#include "../include/Sort.h"
#include "../MPI_Modules/Scheduler.hpp"
#include "../MPI_Modules/serialize/oarchive.hpp"
#include "../MPI_Modules/serialize/iarchive.hpp"
#include "../MPI_Modules/serialize/stream.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
#include <unordered_set>

#include <istream>
#include <sstream>
#include <iterator>
#include <string>

class MyClass
{
	friend class archive::oarchive;
	friend class archive::iarchive;

private:
	template <class Archive>
	void serialize(Archive &ar)
	{
		printf("Hello");
		ar << e1;
		ar << e2;
	}

	template <class Archive>
	void unserialize(Archive &ar)
	{
		ar >> e1;
		ar >> e2;
	}

public:
	MyClass(/* args */)
	{
		srand(time(NULL));
	}
	~MyClass() {}

	std::vector<int> e1;
	std::set<int> e2;
};

namespace fs = std::filesystem;

void print(std::vector<size_t> &ordered)
{

	std::fstream file;
	std::string path = "ouput";
	std::string name = std::to_string(ordered.size()) + ".txt";
	if (!fs::is_directory(path))
	{
		fs::create_directory(path);
	}
	file.open(path + "/" + name, std::ofstream::in | std::ofstream::out | std::ofstream::app);

	auto it = ordered.begin();
	while (it != ordered.end())
	{
		file << *it << "\n";
		it++;
	}

	file.close();
}

int main(int argc, char **argv)
{

	Sort objet;

	library::BranchHandler &handler = library::BranchHandler::getInstance();

	//buildUnsorted(10, 50000000);
	//return 0;

	library::Scheduler scheduler;

	std::set<double> mySet{15.516505, 1.56156156, 56.30501505};
	std::unordered_set<double> myUSet{15.516505, 1.56156156, 56.30501505};

	std::list<float> myList{15.516505, 1.56156156, 56.30501505};
	std::vector<size_t> arr;
	std::vector<size_t> sorted;
	read(arr, "input/1000.txt");

	archive::stream os;
	archive::oarchive oa(os);

	std::queue<float> myQueue;
	myQueue.push(15.516505);
	myQueue.push(1.56156156);
	myQueue.push(56.30501505);

	//library::Serialize instance(oa);
	//auto raw = instance.serialize(-1, arr);
	MyClass ins;
	ins.e1 = {1, 2, 3, 4, 5};
	ins.e2 = {6, 7, 8, 9, 10};
	double id = -1.156;

	oa << id;
	oa << ins;
	//oa << mySet;
	//oa << myUSet;
	//oa << id << arr;
	//oa << myQueue;

	archive::stream is(os);
	//is = os;
	archive::iarchive ia(is);

	double id_received;
	MyClass ins_received;

	ia >> id_received;
	ia >> ins_received;

	//std::set<double> output;
	//std::list<float> output;
	//std::queue<float> output;
	//ia >> output;
	//ia >> id_received >> sorted;
	//auto oarchive = instance.get_oarchive();
	//instance.unserialize(*raw, id, sorted);

	return 0;

	auto _f = std::bind(&Sort::mergeSort, objet, 0, arr);
	scheduler.start(argc, argv, handler, _f, -1, arr);
	return 0;

	objet.setUnsorted(arr);

	//mergeSort(-1, arr, 0, arr.size() - 1);
	//std::vector<size_t> arr{ 12, 11, 13, 5, 6, 7 };
	objet.sort();
	//arr = mergeSort(-1, arr);
	objet.printLog();

	sorted = objet.fetch();

	//print(sorted);

	return 0;
}
