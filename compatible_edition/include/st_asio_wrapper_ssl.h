/*
 * st_asio_wrapper_ssl_object.h
 *
 *  Created on: 2012-3-2
 *      Author: youngwolf
 *		email: mail2tao@163.com
 *		QQ: 676218192
 *		Community on QQ: 198941541
 *
 * make st_asio_wrapper support asio::ssl
 */

#ifndef ST_ASIO_WRAPPER_SSL_H_
#define ST_ASIO_WRAPPER_SSL_H_

#include <boost/asio/ssl.hpp>

#include "st_asio_wrapper_object_pool.h"
#include "st_asio_wrapper_tcp_client.h"
#include "st_asio_wrapper_server.h"

#ifndef DEFAULT_SSL_METHOD
#define DEFAULT_SSL_METHOD boost::asio::ssl::context::sslv3
#endif

namespace st_asio_wrapper
{

template <typename MsgType = std::string, typename Socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>,
	typename Packer = DEFAULT_PACKER, typename Unpacker = DEFAULT_UNPACKER>
class st_ssl_connector_base : public st_connector_base<MsgType, Socket, Packer, Unpacker>
{
public:
	st_ssl_connector_base(boost::asio::io_service& io_service_, boost::asio::ssl::context& ctx) :
		st_connector_base<MsgType, Socket, Packer, Unpacker>(io_service_, ctx), authorized_(false) {}

	//reset all, be ensure that there's no any operations performed on this st_ssl_connector_base when invoke it
	//notice, when reuse this st_ssl_connector_base, st_object_pool will invoke reset(), child must re-write this to initialize
	//all member variables, and then do not forget to invoke st_ssl_connector_base::reset() to initialize father's
	//member variables
	virtual void reset() {authorized_ = false; st_connector_base<MsgType, Socket, Packer, Unpacker>::reset();}

	bool authorized() const {return authorized_;}

protected:
	virtual bool do_start() //connect or receive
	{
		if (!ST_THIS get_io_service().stopped())
		{
			if (ST_THIS reconnecting && !ST_THIS is_connected())
				ST_THIS lowest_layer().async_connect(ST_THIS server_addr,
					boost::bind(&st_ssl_connector_base::connect_handler, this, boost::asio::placeholders::error));
			else if (!authorized_)
				ST_THIS next_layer().async_handshake(boost::asio::ssl::stream_base::client,
					boost::bind(&st_ssl_connector_base::handshake_handler, this, boost::asio::placeholders::error));
			else
				ST_THIS do_recv_msg();

			return true;
		}

		return false;
	}

	virtual void on_unpack_error()
		{authorized_ = false; st_connector_base<MsgType, Socket, Packer, Unpacker>::on_unpack_error();}
	virtual void on_recv_error(const boost::system::error_code& ec)
		{authorized_ = false; st_connector_base<MsgType, Socket, Packer, Unpacker>::on_recv_error(ec);}
	virtual void on_handshake(bool result)
	{
		if (result)
			unified_out::info_out("handshake success.");
		else
		{
			unified_out::error_out("handshake failed!");
			ST_THIS force_close(false);
		}
	}
	virtual bool is_send_allowed() const
		{return authorized() && st_connector_base<MsgType, Socket, Packer, Unpacker>::is_send_allowed();}

	void connect_handler(const boost::system::error_code& ec)
	{
		if (!ec)
		{
			ST_THIS connected = true;
			ST_THIS reconnecting = false;
			ST_THIS on_connect();
			do_start();
		}
		else
			st_connector_base<MsgType, Socket, Packer, Unpacker>::connect_handler(ec);
	}

	void handshake_handler(const boost::system::error_code& ec)
	{
		on_handshake(!ec);
		if (!ec)
		{
			authorized_ = true;
			ST_THIS send_msg(); //send msg buffer may have msgs, send them
			do_start();
		}
	}

protected:
	bool authorized_;
};
typedef st_ssl_connector_base<> st_ssl_connector;
typedef st_sclient<st_ssl_connector> st_ssl_tcp_sclient;

template<typename Object>
class st_ssl_object_pool : public st_object_pool<Object>
{
public:
	st_ssl_object_pool(st_service_pump& service_pump_) :
		st_object_pool<Object>(service_pump_), ctx(DEFAULT_SSL_METHOD) {}
	st_ssl_object_pool(st_service_pump& service_pump_, boost::asio::ssl::context::method m) :
		st_object_pool<Object>(service_pump_), ctx(m) {}

	boost::asio::ssl::context& ssl_context() {return ctx;}

	//this method simply create a class derived from st_socket from heap, secondly you must invoke
	//bool add_client(typename st_client::object_ctype&, bool) before this socket can send or receive msgs.
	//for st_udp_socket, you also need to invoke set_local_addr() before add_client(), please note
	typename st_ssl_object_pool::object_type create_object()
	{
		BOOST_AUTO(client_ptr, ST_THIS reuse_object());
		return client_ptr ? client_ptr : boost::make_shared<Object>(boost::ref(ST_THIS service_pump), boost::ref(ctx));
	}
	template<typename Arg>
	typename st_ssl_object_pool::object_type create_object(Arg& arg)
	{
		BOOST_AUTO(client_ptr, ST_THIS reuse_object());
		return client_ptr ? client_ptr : boost::make_shared<Object>(arg, boost::ref(ctx));
	}

protected:
	boost::asio::ssl::context ctx;
};
typedef st_tcp_client_base<st_ssl_connector, st_ssl_object_pool<st_ssl_connector> > st_ssl_tcp_client;

template<typename MsgType = std::string, typename Socket = boost::asio::ssl::stream<boost::asio::ip::tcp::socket>,
	typename Server = i_server, typename Packer = DEFAULT_PACKER, typename Unpacker = DEFAULT_UNPACKER>
class st_ssl_server_socket_base : public st_server_socket_base<MsgType, Socket, Server, Packer, Unpacker>
{
public:
	st_ssl_server_socket_base(Server& server_, boost::asio::ssl::context& ctx) :
		st_server_socket_base<MsgType, Socket, Server, Packer, Unpacker>(server_, ctx) {}
};
typedef st_ssl_server_socket_base<> st_ssl_server_socket;

template<typename Socket = st_ssl_server_socket, typename Pool = st_ssl_object_pool<Socket>, typename Server = i_server>
class st_ssl_server_base : public st_server_base<Socket, Pool, Server>
{
public:
	st_ssl_server_base(st_service_pump& service_pump_) : st_server_base<Socket, Pool, Server>(service_pump_) {}
	st_ssl_server_base(st_service_pump& service_pump_, boost::asio::ssl::context::method m) :
		st_server_base<Socket, Pool, Server>(service_pump_, m) {}

protected:
	virtual void on_handshake(bool result, typename st_ssl_server_base::object_ctype& client_ptr)
	{
		if (result)
			client_ptr->show_info("handshake with", "success.");
		else
			client_ptr->show_info("handshake with", "failed!");
	}

	virtual void start_next_accept()
	{
		BOOST_TYPEOF(ST_THIS create_object()) client_ptr = ST_THIS create_object(boost::ref(*this));
		ST_THIS acceptor.async_accept(client_ptr->lowest_layer(), boost::bind(&st_ssl_server_base::accept_handler, this,
			boost::asio::placeholders::error, client_ptr));
	}

protected:
	void accept_handler(const boost::system::error_code& ec, typename st_ssl_server_base::object_ctype& client_ptr)
	{
		if (!ec)
		{
			if (ST_THIS on_accept(client_ptr))
				client_ptr->next_layer().async_handshake(boost::asio::ssl::stream_base::server,
					boost::bind(&st_ssl_server_base::handshake_handler, this,
					boost::asio::placeholders::error, client_ptr));

			start_next_accept();
		}
		else
			ST_THIS stop_listen();
	}

	void handshake_handler(const boost::system::error_code& ec, typename st_ssl_server_base::object_ctype& client_ptr)
	{
		on_handshake(!ec, client_ptr);
		if (!ec)
		{
			if (ST_THIS add_client(client_ptr))
			{
				client_ptr->start();
				return;
			}
			else
				client_ptr->show_info("client:", "been refused cause of too many clients.");
		}

		client_ptr->force_close();
	}
};
typedef st_ssl_server_base<> st_ssl_server;

} //namespace

#endif /* ST_ASIO_WRAPPER_SSL_H_ */
