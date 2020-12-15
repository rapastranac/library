#include "../include/main.h"
#include "../include/Sort.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace fs = std::filesystem;

void print(std::vector<size_t>& ordered) {

	std::fstream file;
	std::string path = "ouput";
	std::string name = std::to_string(ordered.size()) + ".txt";
	if (!fs::is_directory(path))
	{
		fs::create_directory(path);
	}
	file.open(path + "/" + name, std::ofstream::in | std::ofstream::out | std::ofstream::app);

	auto it = ordered.begin();
	while (it != ordered.end()) {
		file << *it << "\n";
		it++;
	}

	file.close();

}

int main()
{
	/*buildUnsorted(10, 50000000);
	return 0;*/

	Sort objet;

	std::vector<size_t> arr;
	std::vector<size_t> sorted;
	read(arr, "input/1000.txt");

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
