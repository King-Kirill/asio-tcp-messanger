#include <iostream>
#include <string>
#include <vector>

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class tcp_connection;

// This class represents a user connection. It contains a pointer to a tcp_connection object, 
// which represents the TCP connection associated with the user. 
// The constructor takes a pointer to a tcp_connection object and initializes the conn member variable with it.
class user_connection
{
public:
    user_connection()
    {
    }

    user_connection(std::string name, std::shared_ptr<tcp_connection> conn)
    {
        this->socket_name = name;
		this->conn = conn;
	}
    std::string socket_name;
	std::weak_ptr<tcp_connection> conn;
};

// We will use shared_ptr and enable_shared_from_this 
// because we want to keep the tcp_connection object alive as long as there is an operation that refers to it.
class tcp_connection
    : public std::enable_shared_from_this<tcp_connection>
{
private:
    tcp::socket socket_;
	boost::asio::streambuf buffer_;

	// The constructor is private because we want to control the creation of tcp_connection objects through the create function.
    tcp_connection(boost::asio::io_context& io_context)
        : socket_(io_context)
    {
    }

	// We will use a vector to store the user connections.
    inline static std::vector<user_connection> users;

	// This function is called when the asynchronous write operation is complete. 
    // It takes two parameters: an error code and the number of bytes transferred. 
    // In this example, we don't do anything in this function, but in a real application, 
    // you might want to check for errors or perform some other action.
    void handle_write(const boost::system::error_code& /*error*/,
        size_t /*bytes_transferred*/)
    {
    }

    void send(std::string message)
    {
        // This starts an asynchronous write operation.
        // In simple terms :
        // "Boost.Asio, send this data through this socket, and when the operation finishes, call this callback."

		// We use a shared_ptr to manage the lifetime of the data being sent.
        auto data = std::make_shared<std::string>(std::move(message));

        boost::asio::async_write(
            socket_,
            boost::asio::buffer(*data),
            // Save shared_ptr on this tcp_connection inside callback.
            // Здесь вопрос уже в lifetime: пока async_write выполняется, кто-то должен гарантировать, что message жив.
            [data]
            (
                const boost::system::error_code& error,
                std::size_t bytes_transferred
            )
            {
                if (error)
                {
                    std::cout << error.message() << '\n';
                }
                else
                {
					std::cout << "Sent " << bytes_transferred << " bytes\n";
                }
            }
        );
    }

public:
    typedef std::shared_ptr<tcp_connection> pointer;

    // This function creates a new tcp_connection object and returns a shared pointer to it.
    static pointer create(boost::asio::io_context& io_context)
    {
        return pointer(new tcp_connection(io_context));
    }

    tcp::socket& socket()
    {
        return socket_;
    }

    // creating user
    void create_user(std::string name)
    {
		std::cout << "Creating user on address: " << shared_from_this()->socket().remote_endpoint() << " - with name: " << name << std::endl;
        users.push_back({
             name,
             // takes created earlier make_shared ptr
             shared_from_this()
        });
    }

    // sending msg
    void send_msg(std::string message, std::string receiver)
    {
		std::cout << "Sending message: " << message << " to user: " << receiver << std::endl;

        for (auto& user : users)
        {
            if (user.socket_name == receiver)
            {
                if (auto conn = user.conn.lock())
                {
                    conn->send(message + "\n");
                }

                return;
            }
        }
    }

    // the function reads bytes
    void handle_read(const boost::system::error_code& error, size_t bytes_transferred)
    {
        if (!error)
        {
            // reading message line
            std::istream is(&buffer_);
            std::string message;
            std::getline(is, message);
            
            // creating our own protocols to create user and send messages
            if (message.starts_with("CREATE_USER|"))
            {
                auto delimiter = message.find('|');

                std::string command = message.substr(0, delimiter);
                std::string username = message.substr(delimiter + 1);

                create_user(username);
            }
            else if (message.starts_with("SEND_MSG|"))
            {
                auto delimiter = message.find('|');
                auto second_delimiter = message.find('|', delimiter + 1);

                std::string command =
                    message.substr(0, delimiter);

                std::string receiver =
                    message.substr(
                        delimiter + 1,
                        second_delimiter - delimiter - 1
                    );

                std::string response =
                    message.substr(second_delimiter + 1);

                send_msg(response, receiver);
            }

            // Start reading the next request (next line)
            read_request();
        }
        else
        {
            // Handle error (e.g., client disconnected)
            std::cout << "Error on receive: " << error.message() << std::endl;
        }
	}

    // This function starts the asynchronous read operation. 
    // It uses the async_read_until function to read data from the socket until a newline character is encountered. 
    // When the read operation is complete, the handle_read function will be called.
    void read_request()
    {
        boost::asio::async_read_until(
            socket_,
            buffer_,
            '\n',
            std::bind(
                &tcp_connection::handle_read,
                shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred
            )
        );
	}

    void start()
    {
        read_request();
    }
};


// This class represents a single TCP connection from a client.
class tcp_server {
    private:
         boost::asio::io_context& io_context_;
         tcp::acceptor acceptor_;

		 // This function is called when a new connection is accepted.
         void handle_accept(tcp_connection::pointer new_connection,
             const boost::system::error_code& error)
         {
             if (!error)
             {
                 std::cout << "restarting new connection" << std::endl;
                 new_connection->start();
             }

             start_accept();
         }

		 // This function starts an asynchronous accept operation to wait for a new connection.
         void start_accept()
         {
			 // Create a new connection to handle the incoming connection.
			 // This operation doesn't block the server, it will continue to run and accept new connections.
             tcp_connection::pointer new_connection =
                 tcp_connection::create(io_context_);
			 std::cout << "New connection accepted " << std::endl;
			 // Start an asynchronous accept operation. When a new connection is accepted, the handle_accept function will be called.
			 // This is a non-blocking operation, meaning the server can continue to run and accept new connections while waiting for a new connection to be accepted.
			 // Because there is async, without it the server will not be able to accept new connections while waiting for a new connection to be accepted.
             acceptor_.async_accept(new_connection->socket(),
                 std::bind(&tcp_server::handle_accept, this, new_connection,
                     boost::asio::placeholders::error));
         }

    public:
		 // Constructor for the Tcp_server class. It initializes the acceptor to listen on the specified endpoint 127.0.0.1:8000 and starts accepting connections.
         tcp_server(boost::asio::io_context& io_context)
            : io_context_(io_context),
            acceptor_(io_context, tcp::endpoint(
                boost::asio::ip::make_address("127.0.0.1"),
                8000
            ))
         {
			std::cout << "Server started" << std::endl;
            start_accept();
	     }
};

int main()
{
    try {
        // creating io bound contetx
        boost::asio::io_context io_context;
        tcp_server server(io_context);
        
		std::cout << "Server started on 127.0.0.1:8000" << std::endl;
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << "\n";
	}
}