#ifdef VC_NON_VOID_MPI

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


class VC_non_void_MPI : public VertexCover
{
private:
    /* data */
public:
    VC_non_void_MPI(/* args */) {}
    ~VC_non_void_MPI() {}
};

#endif