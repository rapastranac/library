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
		ar << e3;
	}

	template <class Archive>
	void unserialize(Archive &ar)
	{
		ar >> e1;
		ar >> e2;
		ar >> e3;
	}

public:
	MyClass(/* args */)
	{
		srand(time(NULL));
	}
	~MyClass() {}

	std::vector<int> e1;
	std::set<int> e2;
	std::string e3;
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

	std::vector<size_t> arr;
	std::vector<size_t> sorted;
	read(arr, "input/1000.txt");

	archive::stream os;		  // stream to be sent
	archive::oarchive oa(os); // archive in charge of serializing

	double id = -1.156;
	std::string str{"This is a sentence"};
	std::unordered_set<double> myUSet{15.516505, 1.56156156, 56.30501505};
	//std::map<int, float> myMap = {{5, 5.3543}, {3, 1.3423}, {7, 9.23423}};
	std::map<char, std::set<int>> myMap = {{'f', {3, 5, 34, 3}}, {'d', {5, 67, 4}}, {'j', {5, 67, 34}}};
	MyClass ins;
	ins.e1 = {1, 2, 3, 4, 5};
	ins.e2 = {6, 7, 8, 9, 10};
	ins.e3 = "This is a whole sentence";
	std::set<double> mySet{15.516505, 1.56156156, 56.30501505};
	std::queue<float> myQueue;
	myQueue.push(15.516505);
	myQueue.push(1.56156156);
	myQueue.push(56.30501505);
	std::list<float> myList{15.516505, 1.56156156, 56.30501505};

	oa << id;
	oa << str;
	oa << myMap;
	oa << ins;
	oa << mySet;
	oa << myQueue;
	oa << myList;

	archive::stream is(os);	  // stream to received bytes
	archive::iarchive ia(is); // archive in charge of deserializing

	double id_received;
	std::string str_received;
	std::map<char, std::set<int>> myMap_received;
	MyClass ins_received;
	std::set<double> mySet_received;
	std::queue<float> myQueue_received;
	std::list<float> myList_received;

	ia >> id_received;
	ia >> str_received;
	ia >> myMap_received;
	ia >> ins_received;
	ia >> mySet_received;
	ia >> myQueue_received;
	ia >> myList_received;

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
