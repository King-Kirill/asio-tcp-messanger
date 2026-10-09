#include <iostream>
#include <string>
#include <vector>
#include <csignal>

#ifdef _WIN32
#define _WIN32_WINNT 0x0A00
#endif
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

boost::asio::streambuf buffer;

int main()
{
	bool menu_loop = true;
	bool chat_loop = true;
	std::atomic<bool> client_is_running = true;

	// Create an io_context object to provide I/O services.
	boost::asio::io_context io_context;

	tcp::socket socket(io_context);

	// Create a work guard to keep the io_context running.
	auto guard = boost::asio::make_work_guard(io_context);

	tcp::endpoint endpoint(
		boost::asio::ip::make_address("127.0.0.1"),
		8000
	);

	socket.async_connect(
		endpoint,
		[](const boost::system::error_code& error)
		{
			if (!error)
			{
				std::cout << "Connected to the server on 127.0.0.1:8000 succsessfully!\n";
			}
		}
	);

	// run io context in a separate thread
	std::thread io_thread([&io_context]()
	{
		io_context.run();
		std::cout << "IO RUN FINISHED" << std::endl;
	});

	std::string recipient;

	while (menu_loop)
	{
		int user_input;

		std::cout << "Enter number what u would like to do" << std::endl;
		std::cout << "1 - Create new user" << std::endl;
		std::cout << "2 - Send message to a user" << std::endl;
		std::cout << "3 - Quit" << std::endl;
		std::cin >> user_input;

		if (user_input == 1)
		{
			std::string username;
			std::cout << "Enter username: ";
			std::cin >> username;
			std::string request = "CREATE_USER|" + username + "\n";
			boost::asio::write(
				socket,
				boost::asio::buffer(request)
			);
		}
		else if (user_input == 2)
		{
			std::cout << "Enter recipient username: ";
			std::cin >> recipient;
			menu_loop = false;
		}
		else if (user_input == 3)
		{
			std::cout << "Good buy!!!" << std::endl;
			menu_loop = false;
		}
		else
		{
			std::cout << "Invalid input. Please try again." << std::endl;
		}
	}

	std::function<void()> read;

	read = [&]()
		{
			boost::asio::async_read_until(
				socket,
				buffer,
				'\n',
				[&](const boost::system::error_code& error, std::size_t bytes)
				{
					if (!error)
					{
						std::istream is(&buffer);

						std::string message;

						if (std::getline(is, message))
						{
							std::cout << recipient << ">> "
								<< message << '\n';
						}

						read();
					}
					else
					{
						std::cout << "Read error: "
							<< error.message() << '\n';
					}
				}
			);
		};

	read();

	std::cout << "Welcome to chat!!!" << std::endl;
	std::cout << "Enter message to send to " << recipient << " (or type 'exit' to quit): " << std::endl;

	while (chat_loop)
	{
		std::string message;
		std::cout << "You>> ";
		std::cin >> message;

		if (message == "exit")
		{
			chat_loop = false;
			client_is_running = false;
			socket.close();
			break;
		}
		else
		{
			std::string request = "SEND_MSG|" + recipient + "|" + message + "\n";
			boost::asio::write(
				socket,
				boost::asio::buffer(request)
			);
		}
	}

	io_thread.join();
}