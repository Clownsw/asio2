/*
 * COPYRIGHT (C) 2017-2021, zhllxt
 *
 * author   : zhllxt
 * email    : 37792738@qq.com
 * 
 * Distributed under the GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
 * (See accompanying file LICENSE or see <http://www.gnu.org/licenses/>)
 */

#ifndef __ASIO2_TCP_SESSION_HPP__
#define __ASIO2_TCP_SESSION_HPP__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <asio2/base/detail/push_options.hpp>

#include <asio2/base/session.hpp>

#include <asio2/tcp/impl/tcp_keepalive_cp.hpp>
#include <asio2/tcp/impl/tcp_send_op.hpp>
#include <asio2/tcp/impl/tcp_recv_op.hpp>

namespace asio2::detail
{
	struct template_args_tcp_session
	{
		static constexpr bool is_session = true;
		static constexpr bool is_client  = false;
		static constexpr bool is_server  = false;

		using socket_t    = asio::ip::tcp::socket;
		using buffer_t    = asio::streambuf;
		using send_data_t = std::string_view;
		using recv_data_t = std::string_view;
	};

	ASIO2_CLASS_FORWARD_DECLARE_BASE;
	ASIO2_CLASS_FORWARD_DECLARE_TCP_BASE;
	ASIO2_CLASS_FORWARD_DECLARE_TCP_SERVER;
	ASIO2_CLASS_FORWARD_DECLARE_TCP_SESSION;

	template<class derived_t, class args_t = template_args_tcp_session>
	class tcp_session_impl_t
		: public session_impl_t   <derived_t, args_t>
		, public tcp_keepalive_cp <derived_t, args_t>
		, public tcp_send_op      <derived_t, args_t>
		, public tcp_recv_op      <derived_t, args_t>
	{
		ASIO2_CLASS_FRIEND_DECLARE_BASE;
		ASIO2_CLASS_FRIEND_DECLARE_TCP_BASE;
		ASIO2_CLASS_FRIEND_DECLARE_TCP_SERVER;
		ASIO2_CLASS_FRIEND_DECLARE_TCP_SESSION;

	public:
		using super = session_impl_t    <derived_t, args_t>;
		using self  = tcp_session_impl_t<derived_t, args_t>;

		using args_type   = args_t;
		using key_type    = std::size_t;
		using buffer_type = typename args_t::buffer_t;
		using send_data_t = typename args_t::send_data_t;
		using recv_data_t = typename args_t::recv_data_t;

	public:
		/**
		 * @brief constructor
		 */
		explicit tcp_session_impl_t(
			session_mgr_t<derived_t> & sessions,
			listener_t               & listener,
			io_t                     & rwio,
			std::size_t                init_buf_size,
			std::size_t                max_buf_size
		)
			: super(sessions, listener, rwio, init_buf_size, max_buf_size, rwio.context())
			, tcp_keepalive_cp<derived_t, args_t>()
			, tcp_send_op     <derived_t, args_t>()
			, tcp_recv_op     <derived_t, args_t>()
			, rallocator_()
			, wallocator_()
		{
			this->set_silence_timeout(std::chrono::milliseconds(tcp_silence_timeout));
			this->set_connect_timeout(std::chrono::milliseconds(tcp_connect_timeout));
		}

		/**
		 * @brief destructor
		 */
		~tcp_session_impl_t()
		{
		}

	protected:
		/**
		 * @brief start the session for prepare to recv/send msg
		 */
		template<typename C>
		inline void start(std::shared_ptr<ecs_t<C>> ecs)
		{
		#if defined(ASIO2_ENABLE_LOG)
			// Used to test whether the behavior of different compilers is consistent
			static_assert(tcp_send_op<derived_t, args_t>::template has_member_dgram<self>::value,
				"The behavior of different compilers is not consistent");
		#endif

			ASIO2_ASSERT(this->sessions().io().running_in_this_thread());

		#if defined(_DEBUG) || defined(DEBUG)
			this->is_stop_silence_timer_called_ = false;
			this->is_stop_connect_timeout_timer_called_ = false;
			this->is_disconnect_called_ = false;
		#endif

			std::shared_ptr<derived_t> this_ptr = this->derived().selfptr();

			state_t expected = state_t::stopped;
			if (!this->state_.compare_exchange_strong(expected, state_t::starting))
			{
				this->derived()._do_disconnect(asio::error::already_started, std::move(this_ptr));
				return;
			}

			// must read/write ecs in the io_context thread.
			this->derived().ecs_ = ecs;

			this->derived()._do_init(this_ptr, ecs);

			this->derived()._fire_accept(this_ptr);

			expected = state_t::starting;
			if (!this->state_.compare_exchange_strong(expected, state_t::starting))
			{
				this->derived()._do_disconnect(asio::error::operation_aborted, std::move(this_ptr));
				return;
			}

			if (!this->derived().socket().is_open())
			{
				this->derived()._do_disconnect(asio::error::operation_aborted, std::move(this_ptr));
				return;
			}

			// First call the base class start function
			super::start();

			// if the ecs has remote data call mode,do some thing.
			this->derived()._rdc_init(ecs);

			this->derived()._handle_connect(
				error_code{}, std::move(this_ptr), std::move(ecs), defer_event<void, derived_t>{});
		}

	public:
		/**
		 * @brief stop session
		 * note : this function must be noblocking,if it's blocking,maybe cause circle lock.
		 * You can call this function on the communication thread and anywhere to stop the session.
		 */
		inline void stop()
		{
			this->derived()._do_disconnect(asio::error::operation_aborted, this->derived().selfptr());
		}

		/**
		 * @brief get this object hash key,used for session map
		 */
		inline key_type hash_key() const noexcept
		{
			return reinterpret_cast<key_type>(this);
		}

	protected:
		template<class T, class R, class... Args>
		struct condition_has_member_init : std::false_type {};

		template<class T, class... Args>
		struct condition_has_member_init<T, decltype(std::declval<std::decay_t<T>>().
			init((std::declval<Args>())...)), Args...> : std::true_type {};

		template<typename C>
		inline void _do_init(std::shared_ptr<derived_t>& this_ptr, std::shared_ptr<ecs_t<C>>& ecs) noexcept
		{
			detail::ignore_unused(this_ptr, ecs);

			// reset the variable to default status
			this->derived().reset_connect_time();
			this->derived().update_alive_time();

			if constexpr (std::is_same_v<typename ecs_t<C>::condition_lowest_type, use_dgram_t>)
			{
				this->dgram_ = true;
			}
			else
			{
				this->dgram_ = false;
			}

			using condition_lowest_type = typename detail::remove_cvref_t<typename ecs_t<C>::condition_lowest_type>;

			if constexpr (std::is_class_v<condition_lowest_type>)
			{
				if constexpr (condition_has_member_init<condition_lowest_type, void, std::shared_ptr<derived_t>&>::value)
				{
					ecs->get_condition().lowest().init(this_ptr);
				}
				else
				{
				}
			}
			else
			{
			}

			// set keeplive options
			this->derived().set_keep_alive_options();
		}

		template<typename C, typename DeferEvent>
		inline void _do_start(std::shared_ptr<derived_t> this_ptr, std::shared_ptr<ecs_t<C>> ecs, DeferEvent chain)
		{
			derived_t& derive = this->derived();

			// Beacuse we ensured that the session::_do_disconnect must be called in the session's
			// io_context thread, so if the session::stop is called in the server's bind_connect 
			// callback, the session's disconnect event maybe still be called, However, in this case,
			// we do not want the disconnect event to be called, so at here, we need use asio::post
			// to ensure the join session is must be executed after the disconnect event, otherwise,
			// the join session maybe executed before the disconnect event(the bind_disconnect callback).
			// if the join session is executed before the disconnect event, the bind_disconnect will
			// be called.
			asio::dispatch(derive.io().context(), make_allocator(derive.wallocator(),
			[&derive, this_ptr = std::move(this_ptr), ecs = std::move(ecs), chain = std::move(chain)]
			() mutable
			{
				if (!derive.is_started())
				{
					derive._do_disconnect(asio::error::operation_aborted, std::move(this_ptr), std::move(chain));
					return;
				}

				derive._join_session(std::move(this_ptr), std::move(ecs), std::move(chain));
			}));
		}

		template<typename DeferEvent>
		inline void _handle_disconnect(const error_code& ec, std::shared_ptr<derived_t> this_ptr, DeferEvent chain)
		{
			ASIO2_ASSERT(this->derived().io().running_in_this_thread());
			ASIO2_ASSERT(this->state_ == state_t::stopped);

			set_last_error(ec);

			this->derived()._rdc_stop();

			error_code ec_ignore{};

			// the socket maybe closed already somewhere else.
			if (this->socket().is_open())
			{
				asio::socket_base::linger linger = this->derived().get_linger();

				// the get_linger maybe change the last error value.
				set_last_error(ec);

				// call socket's close function to notify the _handle_recv function response with error > 0 ,
				// then the socket can get notify to exit
				// Call shutdown() to indicate that you will not write any more data to the socket.
				if (!(linger.enabled() == true && linger.timeout() == 0))
				{
					this->socket().shutdown(asio::socket_base::shutdown_both, ec_ignore);
				}
			}

			// if the socket is basic_stream with rate limit, we should call the cancel,
			// otherwise the rate timer maybe can't canceled, and cause the io_context
			// can't stopped forever, even if the socket is closed already.
			this->socket().cancel(ec_ignore);

			// Call close,otherwise the _handle_recv will never return
			this->socket().close(ec_ignore);

			this->derived()._do_stop(ec, std::move(this_ptr), std::move(chain));
		}

		template<typename DeferEvent>
		inline void _do_stop(const error_code& ec, std::shared_ptr<derived_t> this_ptr, DeferEvent chain)
		{
			this->derived()._post_stop(ec, std::move(this_ptr), std::move(chain));
		}

		template<typename DeferEvent>
		inline void _post_stop(const error_code& ec, std::shared_ptr<derived_t> this_ptr, DeferEvent chain)
		{
			// call the base class stop function
			super::stop();

			// call CRTP polymorphic stop
			this->derived()._handle_stop(ec, std::move(this_ptr), std::move(chain));
		}

		template<typename DeferEvent>
		inline void _handle_stop(const error_code& ec, std::shared_ptr<derived_t> this_ptr, DeferEvent chain)
		{
			detail::ignore_unused(ec, this_ptr, chain);

			ASIO2_ASSERT(this->state_ == state_t::stopped);
		}

		template<typename C, typename DeferEvent>
		inline void _join_session(
			std::shared_ptr<derived_t> this_ptr, std::shared_ptr<ecs_t<C>> ecs, DeferEvent chain)
		{
			this->sessions_.emplace(this_ptr,
			[this, this_ptr, ecs = std::move(ecs), chain = std::move(chain)](bool inserted) mutable
			{
				if (inserted)
					this->derived()._start_recv(std::move(this_ptr), std::move(ecs), std::move(chain));
				else
					this->derived()._do_disconnect(
						asio::error::address_in_use, std::move(this_ptr), std::move(chain));
			});
		}

		template<typename C, typename DeferEvent>
		inline void _start_recv(
			std::shared_ptr<derived_t> this_ptr, std::shared_ptr<ecs_t<C>> ecs, DeferEvent chain)
		{
			// to avlid the user call stop in another thread,then it may be socket.async_read_some
			// and socket.close be called at the same time
			asio::dispatch(this->io().context(), make_allocator(this->wallocator_,
			[this, this_ptr = std::move(this_ptr), ecs = std::move(ecs), chain = std::move(chain)]
			() mutable
			{
				using condition_lowest_type = typename ecs_t<C>::condition_lowest_type;

				detail::ignore_unused(chain);

				if constexpr (!std::is_same_v<condition_lowest_type, asio2::detail::hook_buffer_t>)
				{
					this->derived().buffer().consume(this->derived().buffer().size());
				}
				else
				{
					std::ignore = true;
				}

				// start the timer of check silence timeout
				this->derived()._post_silence_timer(this->silence_timeout_, this_ptr);

				this->derived()._post_recv(std::move(this_ptr), std::move(ecs));
			}));
		}

		template<class Data, class Callback>
		inline bool _do_send(Data& data, Callback&& callback)
		{
			return this->derived()._tcp_send(data, std::forward<Callback>(callback));
		}

		template<class Data>
		inline send_data_t _rdc_convert_to_send_data(Data& data) noexcept
		{
			auto buffer = asio::buffer(data);
			return send_data_t{ reinterpret_cast<
				std::string_view::const_pointer>(buffer.data()),buffer.size() };
		}

		template<class Invoker>
		inline void _rdc_invoke_with_none(const error_code& ec, Invoker& invoker)
		{
			if (invoker)
				invoker(ec, send_data_t{}, recv_data_t{});
		}

		template<class Invoker>
		inline void _rdc_invoke_with_recv(const error_code& ec, Invoker& invoker, recv_data_t data)
		{
			if (invoker)
				invoker(ec, send_data_t{}, data);
		}

		template<class Invoker>
		inline void _rdc_invoke_with_send(const error_code& ec, Invoker& invoker, send_data_t data)
		{
			if (invoker)
				invoker(ec, data, recv_data_t{});
		}

	protected:
		template<typename C>
		inline void _post_recv(std::shared_ptr<derived_t> this_ptr, std::shared_ptr<ecs_t<C>> ecs)
		{
			this->derived()._tcp_post_recv(std::move(this_ptr), std::move(ecs));
		}

		template<typename C>
		inline void _handle_recv(
			const error_code& ec, std::size_t bytes_recvd,
			std::shared_ptr<derived_t> this_ptr, std::shared_ptr<ecs_t<C>> ecs)
		{
			this->derived()._tcp_handle_recv(ec, bytes_recvd, std::move(this_ptr), std::move(ecs));
		}

		template<typename C>
		inline void _fire_recv(
			std::shared_ptr<derived_t>& this_ptr, std::shared_ptr<ecs_t<C>>& ecs, std::string_view data)
		{
			this->listener_.notify(event_type::recv, this_ptr, data);

			this->derived()._rdc_handle_recv(this_ptr, ecs, data);
		}

		inline void _fire_accept(std::shared_ptr<derived_t>& this_ptr)
		{
			// the _fire_accept must be executed in the thread 0.
			ASIO2_ASSERT(this->sessions().io().running_in_this_thread());

			this->listener_.notify(event_type::accept, this_ptr);
		}

		template<typename C>
		inline void _fire_connect(std::shared_ptr<derived_t>& this_ptr, std::shared_ptr<ecs_t<C>>& ecs)
		{
			// the _fire_connect must be executed in the thread 0.
			ASIO2_ASSERT(this->sessions().io().running_in_this_thread());

		#if defined(_DEBUG) || defined(DEBUG)
			ASIO2_ASSERT(this->is_disconnect_called_ == false);
		#endif

			this->derived()._rdc_start(this_ptr, ecs);

			this->listener_.notify(event_type::connect, this_ptr);
		}

		inline void _fire_disconnect(std::shared_ptr<derived_t>& this_ptr)
		{
			// the _fire_disconnect must be executed in the thread 0.
			ASIO2_ASSERT(this->sessions().io().running_in_this_thread());

		#if defined(_DEBUG) || defined(DEBUG)
			this->is_disconnect_called_ = true;
		#endif

			this->listener_.notify(event_type::disconnect, this_ptr);
		}

	protected:
		/**
		 * @brief get the recv/read allocator object refrence
		 */
		inline auto & rallocator() noexcept { return this->rallocator_; }
		/**
		 * @brief get the send/write allocator object refrence
		 */
		inline auto & wallocator() noexcept { return this->wallocator_; }

	protected:
		/// The memory to use for handler-based custom memory allocation. used fo recv/read.
		handler_memory<std::true_type , assizer<args_t>>   rallocator_;

		/// The memory to use for handler-based custom memory allocation. used fo send/write.
		handler_memory<std::false_type, assizer<args_t>>   wallocator_;

		/// Does it have the same datagram mechanism as udp?
		bool                                      dgram_                = false;

	#if defined(_DEBUG) || defined(DEBUG)
		bool                                      is_disconnect_called_ = false;
	#endif
	};
}

namespace asio2
{
	using tcp_session_args = detail::template_args_tcp_session;

	template<class derived_t, class args_t>
	using tcp_session_impl_t = detail::tcp_session_impl_t<derived_t, args_t>;

	/**
	 * @brief tcp session
	 */
	template<class derived_t>
	class tcp_session_t : public detail::tcp_session_impl_t<derived_t, detail::template_args_tcp_session>
	{
	public:
		using detail::tcp_session_impl_t<derived_t, detail::template_args_tcp_session>::tcp_session_impl_t;
	};

	/**
	 * @brief tcp session
	 */
	class tcp_session : public tcp_session_t<tcp_session>
	{
	public:
		using tcp_session_t<tcp_session>::tcp_session_t;
	};
}

#if defined(ASIO2_INCLUDE_RATE_LIMIT)
#include <asio2/tcp/tcp_stream.hpp>
namespace asio2
{
	struct tcp_rate_session_args : public tcp_session_args
	{
		using socket_t = asio2::tcp_stream<asio2::simple_rate_policy>;
	};

	template<class derived_t>
	class tcp_rate_session_t : public asio2::tcp_session_impl_t<derived_t, tcp_rate_session_args>
	{
	public:
		using asio2::tcp_session_impl_t<derived_t, tcp_rate_session_args>::tcp_session_impl_t;
	};

	class tcp_rate_session : public asio2::tcp_rate_session_t<tcp_rate_session>
	{
	public:
		using asio2::tcp_rate_session_t<tcp_rate_session>::tcp_rate_session_t;
	};
}
#endif

#include <asio2/base/detail/pop_options.hpp>

#endif // !__ASIO2_TCP_SESSION_HPP__
