/*
 * Copyright (c) 2017-2023 zhllxt
 *
 * author   : zhllxt
 * email    : 37792738@qq.com
 * 
 * Distributed under the Boost Software License, Version 1.0. (See accompanying
 * file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
 */

#if defined(ASIO2_ENABLE_SSL) || defined(ASIO2_USE_SSL)

#ifndef __ASIO2_TCPS_SESSION_HPP__
#define __ASIO2_TCPS_SESSION_HPP__

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include <asio2/base/detail/push_options.hpp>

#include <asio2/tcp/tcp_session.hpp>
#include <asio2/tcp/impl/ssl_stream_cp.hpp>
#include <asio2/tcp/impl/ssl_context_cp.hpp>

namespace asio2::detail
{
	ASIO2_CLASS_FORWARD_DECLARE_BASE;
	ASIO2_CLASS_FORWARD_DECLARE_TCP_BASE;
	ASIO2_CLASS_FORWARD_DECLARE_TCP_SERVER;
	ASIO2_CLASS_FORWARD_DECLARE_TCP_SESSION;

	template<class derived_t, class args_t = template_args_tcp_session>
	class tcps_session_impl_t
		: public tcp_session_impl_t<derived_t, args_t>
		, public ssl_stream_cp     <derived_t, args_t>
	{
		ASIO2_CLASS_FRIEND_DECLARE_BASE;
		ASIO2_CLASS_FRIEND_DECLARE_TCP_BASE;
		ASIO2_CLASS_FRIEND_DECLARE_TCP_SERVER;
		ASIO2_CLASS_FRIEND_DECLARE_TCP_SESSION;

	public:
		using super = tcp_session_impl_t <derived_t, args_t>;
		using self  = tcps_session_impl_t<derived_t, args_t>;

		using args_type   = args_t;
		using key_type    = std::size_t;
		using buffer_type = typename args_t::buffer_t;

		using ssl_stream_comp = ssl_stream_cp<derived_t, args_t>;

		using super::send;
		using super::async_send;

	public:
		/**
		 * @brief constructor
		 */
		explicit tcps_session_impl_t(
			asio::ssl::context       & ctx,
			session_mgr_t<derived_t> & sessions,
			listener_t               & listener,
			io_t                     & rwio,
			std::size_t                init_buf_size,
			std::size_t                max_buf_size
		)
			: super(sessions, listener, rwio, init_buf_size, max_buf_size)
			, ssl_stream_comp(this->io_, ctx, asio::ssl::stream_base::server)
			, ctx_(ctx)
		{
		}

		/**
		 * @brief destructor
		 */
		~tcps_session_impl_t()
		{
		}

	public:
		/**
		 * @brief get this object hash key,used for session map
		 */
		inline key_type hash_key() const noexcept
		{
			return reinterpret_cast<key_type>(this);
		}

		/**
		 * @brief get the stream object refrence
		 */
		inline typename ssl_stream_comp::stream_type & stream() noexcept
		{
			return this->derived().ssl_stream();
		}

	protected:
		template<typename C>
		inline void _do_init(std::shared_ptr<derived_t>& this_ptr, std::shared_ptr<ecs_t<C>>& ecs)
		{
			super::_do_init(this_ptr, ecs);

			this->derived()._ssl_init(ecs, this->socket_, this->ctx_);
		}

		template<typename DeferEvent>
		inline void _handle_disconnect(const error_code& ec, std::shared_ptr<derived_t> this_ptr, DeferEvent chain)
		{
			this->derived()._ssl_stop(this_ptr,
				defer_event
				{
					[this, ec, this_ptr, e = chain.move_event()] (event_queue_guard<derived_t> g) mutable
					{
						super::_handle_disconnect(ec, std::move(this_ptr), defer_event(std::move(e), std::move(g)));
					}, chain.move_guard()
				}
			);
		}

		template<typename C, typename DeferEvent>
		inline void _handle_connect(
			const error_code& ec,
			std::shared_ptr<derived_t> this_ptr, std::shared_ptr<ecs_t<C>> ecs, DeferEvent chain)
		{
			detail::ignore_unused(ec);

			ASIO2_ASSERT(!ec);
			ASIO2_ASSERT(this->derived().sessions().io().running_in_this_thread());

			asio::dispatch(this->derived().io().context(), make_allocator(this->derived().wallocator(),
			[this, this_ptr = std::move(this_ptr), ecs = std::move(ecs), chain = std::move(chain)]
			() mutable
			{
				this->derived()._ssl_start(this_ptr, ecs, this->socket_, this->ctx_);

				this->derived()._post_handshake(std::move(this_ptr), std::move(ecs), std::move(chain));
			}));
		}

		inline void _fire_handshake(std::shared_ptr<derived_t>& this_ptr)
		{
			// the _fire_handshake must be executed in the thread 0.
			ASIO2_ASSERT(this->sessions().io().running_in_this_thread());

			this->listener_.notify(event_type::handshake, this_ptr);
		}

	public:
		inline constexpr static bool is_sslmode() noexcept { return true; }

	protected:
		asio::ssl::context & ctx_;
	};
}

namespace asio2
{
	using tcps_session_args = detail::template_args_tcp_session;

	template<class derived_t, class args_t>
	using tcps_session_impl_t = detail::tcps_session_impl_t<derived_t, args_t>;

	/**
	 * @brief ssl tcp session
	 */
	template<class derived_t>
	class tcps_session_t : public detail::tcps_session_impl_t<derived_t, detail::template_args_tcp_session>
	{
	public:
		using detail::tcps_session_impl_t<derived_t, detail::template_args_tcp_session>::tcps_session_impl_t;
	};

	/**
	 * @brief ssl tcp session
	 */
	class tcps_session : public tcps_session_t<tcps_session>
	{
	public:
		using tcps_session_t<tcps_session>::tcps_session_t;
	};
}

#if defined(ASIO2_INCLUDE_RATE_LIMIT)
#include <asio2/tcp/tcp_stream.hpp>
namespace asio2
{
	struct tcps_rate_session_args : public tcps_session_args
	{
		using socket_t = asio2::tcp_stream<asio2::simple_rate_policy>;
	};

	template<class derived_t>
	class tcps_rate_session_t : public asio2::tcps_session_impl_t<derived_t, tcps_rate_session_args>
	{
	public:
		using asio2::tcps_session_impl_t<derived_t, tcps_rate_session_args>::tcps_session_impl_t;
	};

	class tcps_rate_session : public asio2::tcps_rate_session_t<tcps_rate_session>
	{
	public:
		using asio2::tcps_rate_session_t<tcps_rate_session>::tcps_rate_session_t;
	};
}
#endif

#include <asio2/base/detail/pop_options.hpp>

#endif // !__ASIO2_TCPS_SESSION_HPP__

#endif
