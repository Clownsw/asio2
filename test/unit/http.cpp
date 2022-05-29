#include "unit_test.hpp"
#include <filesystem>
#include <iostream>
#include <asio2/asio2.hpp>

struct aop_log
{
	bool before(http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req, rep);
		return true;
	}
	bool after(std::shared_ptr<asio2::http_session>& session_ptr, http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(session_ptr, req, rep);
		return true;
	}
};

struct aop_check
{
	bool before(std::shared_ptr<asio2::http_session>& session_ptr, http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(session_ptr, req, rep);
		return true;
	}
	bool after(http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req, rep);
		return true;
	}
};

void http_test()
{
	ASIO2_TEST_BEGIN_LOOP(test_loop_times);

	asio2::http_server server;

	server.support_websocket(true);

	// set the root directory, here is:  /asio2/example/wwwroot
	std::filesystem::path root = std::filesystem::current_path()
		.parent_path().parent_path().parent_path()
		.append("example").append("wwwroot");
	server.set_root_directory(std::move(root));

	server.bind_recv([&](http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req, rep);
	}).bind_connect([](auto & session_ptr)
	{
		asio2::ignore_unused(session_ptr);
		//session_ptr->set_response_mode(asio2::response_mode::manual);
	}).bind_disconnect([](auto & session_ptr)
	{
		asio2::ignore_unused(session_ptr);
	}).bind_start([&]()
	{
	}).bind_stop([&]()
	{
	});

	server.bind<http::verb::get, http::verb::post>("/", [](http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req, rep);

		rep.fill_file("index.html");
		rep.chunked(true);

	}, aop_log{});

	// If no method is specified, GET and POST are both enabled by default.
	server.bind("*", [](http::web_request& req, http::web_response& rep)
	{
		rep.fill_file(req.target());
	}, aop_check{});

	// Test http multipart
	server.bind<http::verb::get, http::verb::post>("/multipart", [](http::web_request& req, http::web_response& rep)
	{
		auto& body = req.body();
		auto target = req.target();

		std::stringstream ss;
		ss << req;
		auto str_req = ss.str();

		asio2::ignore_unused(body, target, str_req);

		for (auto it = req.begin(); it != req.end(); ++it)
		{
		}

		auto mpbody = req.multipart();

		for (auto it = mpbody.begin(); it != mpbody.end(); ++it)
		{
		}

		if (req.has_multipart())
		{
			http::multipart_fields multipart_body = req.multipart();

			auto& field_username = multipart_body["username"];
			auto username = field_username.value();

			auto& field_password = multipart_body["password"];
			auto password = field_password.value();

			std::string str = http::to_string(multipart_body);

			std::string type = "multipart/form-data; boundary="; type += multipart_body.boundary();

			rep.fill_text(str, http::status::ok, type);

			http::request_t<http::string_body> re;
			re.method(http::verb::post);
			re.set(http::field::content_type, type);
			re.keep_alive(true);
			re.target("/api/user/");
			re.body() = str;
			re.prepare_payload();

			std::stringstream ress;
			ress << re;
			auto restr = ress.str();

			asio2::ignore_unused(username, password, restr);
		}
		else
		{
			rep.fill_page(http::status::ok);
		}
	}, aop_log{});

	server.bind<http::verb::get>("/del_user",
		[](std::shared_ptr<asio2::http_session>& session_ptr, http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(session_ptr, req, rep);

		rep.fill_page(http::status::ok, "del_user successed.");

	}, aop_check{});

	server.bind<http::verb::get, http::verb::post>("/api/user/*", [](http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req, rep);

		//rep.fill_text("the user name is hanmeimei, .....");

	}, aop_log{}, aop_check{});

	server.bind<http::verb::get>("/defer", [](http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req, rep);

		// use defer to make the reponse not send immediately, util the derfer shared_ptr
		// is destroyed, then the response will be sent.
		std::shared_ptr<http::response_defer> rep_defer = rep.defer();

		std::thread([rep_defer, &rep]() mutable
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));

			rep = asio2::http_client::execute("http://www.baidu.com");
		}).detach();

	}, aop_log{}, aop_check{});

	bool ws_open_flag = false, ws_close_flag = false;
	server.bind("/ws", websocket::listener<asio2::http_session>{}.
		on("message", [](std::shared_ptr<asio2::http_session>& session_ptr, std::string_view data)
	{
		session_ptr->async_send(data);

	}).on("open", [&](std::shared_ptr<asio2::http_session>& session_ptr)
	{
		ws_open_flag = true;
		session_ptr->post([]() {}, std::chrono::milliseconds(10));
		// how to set custom websocket response data : 
		session_ptr->ws_stream().set_option(websocket::stream_base::decorator(
			[](websocket::response_type& rep)
		{
			rep.set(http::field::authorization, " http-server-coro");
		}));

	}).on("close", [&](std::shared_ptr<asio2::http_session>& session_ptr)
	{
		ws_close_flag = true;
		asio2::ignore_unused(session_ptr);
	}).on_ping([](std::shared_ptr<asio2::http_session>& session_ptr)
	{
		asio2::ignore_unused(session_ptr);
	}).on_pong([](std::shared_ptr<asio2::http_session>& session_ptr)
	{
		asio2::ignore_unused(session_ptr);
	}));

	server.bind_not_found([](http::web_request& req, http::web_response& rep)
	{
		asio2::ignore_unused(req);

		rep.fill_page(http::status::not_found);
	});

	server.start("127.0.0.1", 8080);

	asio2::http_client::execute("127.0.0.1", "8080", "/", std::chrono::seconds(5));

	asio2::http_client::execute("127.0.0.1", "8080", "/defer");

	asio2::http_client http_client;

	http_client.bind_recv([&](http::web_request& req, http::web_response& rep)
	{
		// convert the response body to string
		std::stringstream ss;
		ss << rep.body();

		// Remove all fields
		req.clear();

		req.set(http::field::user_agent, "Chrome");
		req.set(http::field::content_type, "text/html");

		req.method(http::verb::get);
		req.keep_alive(true);
		req.target("/get_user?name=abc");
		req.body() = "Hello World.";
		req.prepare_payload();

		http_client.async_send(std::move(req));

	}).bind_connect([&]()
	{
		// connect success, send a request.
		if (!asio2::get_last_error())
		{
			const char * msg = "GET / HTTP/1.1\r\n\r\n";
			http_client.async_send(msg);
		}
	});

	bool http_client_ret = http_client.start("127.0.0.1", 8080);
	ASIO2_CHECK(http_client_ret);

	asio2::ws_client ws_client;

	ws_client.set_connect_timeout(std::chrono::seconds(5));

	ws_client.bind_init([&]()
	{
		// how to set custom websocket request data : 
		ws_client.ws_stream().set_option(websocket::stream_base::decorator(
			[](websocket::request_type& req)
		{
			req.set(http::field::authorization, " websocket-authorization");
		}));

	}).bind_connect([&]()
	{
		std::string s;
		s += '<';
		int len = 128 + std::rand() % 512;
		for (int i = 0; i < len; i++)
		{
			s += (char)((std::rand() % 26) + 'a');
		}
		s += '>';

		ws_client.async_send(std::move(s));

	}).bind_upgrade([&]()
	{
		// this send will be failed, because connection is not fully completed
		ws_client.async_send("abc", [](std::size_t bytes)
		{
			ASIO2_CHECK(asio2::get_last_error() == asio::error::not_connected);
			ASIO2_CHECK(bytes == 0);
		});

	}).bind_recv([&](std::string_view data)
	{
		ws_client.async_send(data);
	});

	bool ws_client_ret = ws_client.start("127.0.0.1", 8080, "/ws");

	ASIO2_CHECK(ws_client_ret);

	ASIO2_CHECK(ws_open_flag);

	std::this_thread::sleep_for(std::chrono::milliseconds(50 + std::rand() % 50));

	ws_client.stop();
	ASIO2_CHECK(ws_close_flag);

	ASIO2_TEST_END_LOOP;
}


ASIO2_TEST_SUITE
(
	"http",
	ASIO2_TEST_CASE(http_test)
)
