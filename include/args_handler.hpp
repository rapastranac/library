#ifndef ARGS_HANDLER_HPP
#define ARGS_HANDLER_HPP

#include "ctpl_stl.hpp"

#include <functional>
#include <iostream>
#include <type_traits>
#include <tuple>
#include <utility>

//This is just a library to handle arguments

namespace std
{
	template <int>
	struct variadic_placeholder
	{
	};

	template <int N>
	struct is_placeholder<variadic_placeholder<N>>
		: integral_constant<int, N + 1>
	{
	};

	class args_handler
	{
	public:
		/*begin<-- this allows to bind a function with unknown number of parameters ---*/

		template <typename F, size_t... Is>
		static auto bind_place_holders(std::index_sequence<Is...>, F &&f)
		{
			return std::bind(std::forward<F>(f), variadic_placeholder<Is>{}...);
		}

		template <typename F, typename... Args>
		static auto bind_place_holders(F &&f, Args &&...args)
		{
			return bind_place_holders(std::make_index_sequence<sizeof...(Args)>{}, std::forward<F>(f));
		}

		/*---- this allows to bind a function with unknown number of parameters --->end*/

		/*begin<---------This detects if return type is void or not -------------------*/
		struct Void
		{
		};

		template <typename F, typename... Args,
				  typename Result = std::invoke_result_t<F, Args...>,
				  std::enable_if_t<!std::is_void_v<Result>, int> = 0>
		static Result invoke_void(F &&f, Args &&...args)
		{
			return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
		}

		// void case
		template <typename F, typename... Args,
				  typename Result = std::invoke_result_t<F, Args...>,
				  std::enable_if_t<std::is_void_v<Result>, int> = 0>
		static Void invoke_void(F &&f, Args &&...args)
		{
			std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
			return Void();
		}
		/*--------------- This detects if return type is void or not ----------------->end*/

		/*begin<----------	This unpacks tuple before pushing to pool -------------------*/
		//tracking with a holder

		template <typename F, typename... Args> //,  // typename std::enable_if<std::is_same<P, ctpl::Pool>::value>::type * = nullptr>
		static auto ignore_holder(ctpl::Pool &pool, F &&f, Args &&...args)
		{

			int size = sizeof...(args); //testing
			/* Maybe a flag to know if passing last argument or not,
				for the cases when no holder is passed*/
			auto fun = [f, &pool](int id, Args &...args, void *last) {
				//id is ignored due to ctpl stuff
				//holder tracker (last parameter) is not passed when pushed
				return pool.push(f, args..., nullptr);
			};
			return invoke_void(fun, 0, args..., nullptr);
			//return Void();
		}

		template <typename F, typename Tuple, size_t... I>
		static auto unpack_tuple(ctpl::Pool &pool, F &&f, Tuple &t, std::index_sequence<I...>, bool trackingStack)
		{
			return ignore_holder(pool, f, std::get<I>(t)...);
			//return Void();
		}

		template <typename F, typename Tuple>
		static auto unpack_tuple(ctpl::Pool &pool, F &&f, Tuple &t, bool trackingStack)
		{
			//https://stackoverflow.com/a/36656413/5248548
			static constexpr auto size = std::tuple_size<Tuple>::value;
			//printf("tuple_size: %d\n", size);
			return unpack_tuple(pool, f, t, std::make_index_sequence<size>{}, trackingStack);
		}

		/*-------------	This unpacks tuple before pushing to pool ---------------->end*/

		/*begin<----------	This unpacks tuple before pushing to pool -------------------*/
		//not tracking

		template <typename F, typename... Args>
		//typename std::enable_if<std::is_same<P, ctpl::Pool>::value>::type* = nullptr>
		static auto ignore_id(ctpl::Pool &pool, F &&f, Args &&...args)
		{

			//int size = sizeof...(args); //testing
			/* Maybe a flag to know if passing last argument or not,
				for the cases when no holder is passed*/
			auto fun = [f, &pool](int, Args &&...args) {
				//id is ignored due to ctpl stuff
				//holder tracker (last parameter) is not passed when pushed
				return pool.push(f, args...);
			};
			return invoke_void(fun, 0, args...);
		}

		template <typename F, typename Tuple, size_t... I>
		static auto unpack_tuple(ctpl::Pool &pool, F &&f, Tuple &t, std::index_sequence<I...>)
		{
			return ignore_id(pool, f, std::get<I>(t)...);
		}

		template <typename F, typename Tuple>
		static auto unpack_tuple(ctpl::Pool &pool, F &&f, Tuple &t)
		{
			//https://stackoverflow.com/a/36656413/5248548
			static constexpr auto size = std::tuple_size<Tuple>::value;
			return unpack_tuple(pool, f, t, std::make_index_sequence<size>{});
		}

		/*-------------	This unpacks tuple before pushing to pool ---------------->end*/

		/*begin<--- This unpacks tuple before forwarding it through the function -----*/
		/* tracking stack */

		template <typename HOLDER, typename Function, typename Tuple, size_t... I>
		static auto unpack_tuple(HOLDER *h, Function &&f, int id, Tuple &t, std::index_sequence<I...>, bool trackingStack)
		{
			return invoke_void(f, id, std::get<I>(t)..., h);
		}

		template <typename HOLDER, typename Function, typename Tuple>
		static auto unpack_tuple(HOLDER *h, Function &&f, int id, Tuple &t, bool trackingStack)
		{
			//https://stackoverflow.com/a/36656413/5248548
			static constexpr auto size = std::tuple_size<Tuple>::value;
			//std::cout << typeid(t).name() << "\n";
			return unpack_tuple(h, f, id, t, std::make_index_sequence<size>{}, trackingStack);
			//return Void();
		}
		/*------- This unpacks tuple before forwarding it through the function ----->end*/

		/*begin<--- This unpacks tuple before forwarding it through the function -----*/
		/* same as above, not tracking stack */ // TO IMPROVE

		template <typename Function, typename Tuple, size_t... I>
		static auto unpack_tuple(Function &&f, int id, Tuple &t, std::index_sequence<I...>)
		{
			return invoke_void(f, id, std::get<I>(t)...);
		}

		template <typename Function, typename Tuple>
		static auto unpack_tuple(Function &&f, int id, Tuple &t)
		{
			//https://stackoverflow.com/a/36656413/5248548
			static constexpr auto size = std::tuple_size<Tuple>::value;
			return unpack_tuple(f, id, t, std::make_index_sequence<size>{});
		}
		/*------- This unpacks tuple before forwarding it through the function ----->end*/

		/*begin<--- General unpack tuple and passes arguments to callable -----*/
		template <typename Function, typename Tuple, size_t... I>
		static auto unpack_tuple(Function &&f, Tuple &t, std::index_sequence<I...>)
		{
			return invoke_void(f, std::get<I>(t)...);
		}

		template <typename Function, typename Tuple>
		static auto unpack_tuple(Function &&f, Tuple &t)
		{
			//https://stackoverflow.com/a/36656413/5248548
			static constexpr auto size = std::tuple_size<Tuple>::value;
			return unpack_tuple(f, t, std::make_index_sequence<size>{});
		}
		/*------- General unpack tuple and passes arguments to callable ----->end*/
	};

} // namespace std
#endif