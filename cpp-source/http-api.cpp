#include <random>

#include "http-api.hpp"

HttpApi::HttpApi(std::string new_public_directory, EpollServer* new_server, SymmetricEncryptor* new_encryptor)
:public_directory(new_public_directory),
server(new_server),
encryptor(new_encryptor)
{
	this->server->set_timeout(10);
}

HttpApi::HttpApi(std::string new_public_directory, EpollServer* new_server)
:public_directory(new_public_directory),
server(new_server),
encryptor(0)
{
	this->server->set_timeout(10);
}

void HttpApi::route(std::string method, std::string path, std::function<std::string(JsonObject*)> function,
std::unordered_map<std::string, JsonType> requires,
std::chrono::milliseconds rate_limit,
bool requires_human){
	if(path[path.length() - 1] != '/'){
		path += '/';
	}
	this->routemap[method + " /api" + path] = new Route(function, requires, rate_limit, requires_human);
}

void HttpApi::route(std::string method, std::string path, std::function<std::string(JsonObject*, JsonObject*)> function,
std::unordered_map<std::string, JsonType> requires,
std::chrono::milliseconds rate_limit,
bool requires_human){
	if(encryptor == 0){
		PRINT("You tried to add a route that uses encryption. Please provide an encryptor in the HttpApi constructor")
		exit(1);
	}
	if(path[path.length() - 1] != '/'){
		path += '/';
	}
	this->routemap[method + " /api" + path] = new Route(function, requires, rate_limit, requires_human);
}

void HttpApi::route(std::string method, std::string path, std::function<ssize_t(JsonObject*, int)> function,
std::unordered_map<std::string, JsonType> requires,
std::chrono::milliseconds rate_limit,
bool requires_human){
	if(path[path.length() - 1] != '/'){
		path += '/';
	}
	this->routemap[method + " /api" + path] = new Route(function, requires, rate_limit, requires_human);
}

struct Question{
	std::string q;
	std::vector<std::string> a;
};
static struct Question questions[6] = {
	{"What is the basic color of the sky?", {"blue"}},
	{"What is the basic color of grass?", {"green"}},
	{"What is the basic color of blood?", {"red"}},
	{"What is the first name of the current president?", {"donald"}},
	{"What is the last name of the current president?", {"trump"}},
	{"How many planets are in the solar system?", {"8", "9"}}
};
static std::random_device rd;
static std::mt19937 mt(rd());
static std::uniform_int_distribution<size_t> dist(0, 5);
static struct Question* get_question(){
	return &questions[dist(mt)];
}
static std::unordered_map<int, struct Question*> client_questions;

void HttpApi::start(void){
	std::string default_header = "HTTP/1.1 200 OK\n"
		"Accept-Ranges: bytes\n";

	this->routes_object = new JsonObject(OBJECT);
	for(auto iter = this->routemap.begin(); iter != this->routemap.end(); ++iter){
		routes_object->objectValues[iter->first] = new JsonObject(OBJECT);
		routes_object->objectValues[iter->first]->objectValues["rate_limit"] =
			new JsonObject(std::to_string(iter->second->minimum_ms_between_call.count()));
		routes_object->objectValues[iter->first]->objectValues["parameters"] = new JsonObject(ARRAY);
		
		for(auto &field : iter->second->requires){
			routes_object->objectValues[iter->first]->objectValues["parameters"]->arrayValues.push_back(new JsonObject(field.first));
		}
		if(iter->second->token_function != nullptr){
			routes_object->objectValues[iter->first]->objectValues["parameters"]->arrayValues.push_back(new JsonObject("token"));
		}
	}
	this->routes_string = routes_object->stringify();

	//PRINT("HttpApi running with routes: " << this->routes_string)
	
	this->server->on_connect = [&](int fd){
		client_questions[fd] = get_question();
	};

	this->server->on_read = [&](int fd, const char* data, ssize_t data_length)->ssize_t{
		//DEBUG("RECV:" << data)
		
		JsonObject r_obj(OBJECT);
		enum RequestResult r_type = Util::parse_http_api_request(data, &r_obj);

		std::string response_header = default_header;
		std::string response_body = std::string();
		std::string response = std::string();
		
		if(r_type == JSON){
			char json_data[PACKET_LIMIT + 32];
			
			auto get_body_callback = [&](int, const char*, size_t dl)->ssize_t{
				DEBUG("GOT JSON:" << json_data);
				return static_cast<ssize_t>(dl);
			};
			
			if(this->server->recv(fd, json_data, PACKET_LIMIT, get_body_callback) <= 0){
				PRINT("FAILED TO GET JSON POST BODY");
				return -1;
			}
			
			r_obj.parse(json_data);
			
			r_type = HTTP_API;
		}
		
		//DEBUG("JSON: " << r_obj.stringify(true))
		
		std::string route = r_obj.GetStr("route");
		
		if(r_type == HTTP){
			std::string clean_route = this->public_directory;
			for(size_t i = 0; i < route.length(); ++i){
				if(i < route.length() - 1 &&
				route[i] == '.' &&
				route[i + 1] == '.'){
					continue;
				}
				clean_route += route[i];
			}
			
			if(this->file_cache.count(clean_route + "/index.html")){
				clean_route += "/index.html";
			}

			struct stat route_stat;
			if(lstat(clean_route.c_str(), &route_stat) < 0){
				// perror("lstat");
				response_body = HTTP_404;
				response_header = response_header.replace(9, 6, "404 Not Found");
			}else if(S_ISDIR(route_stat.st_mode)){
				clean_route += "/index.html";
				if(lstat(clean_route.c_str(), &route_stat) < 0){
					// perror("lstat index.html");
					response_body = HTTP_404;
					response_header = response_header.replace(9, 6, "404 Not Found");
				}
			}

			if(response_body.empty() && S_ISREG(route_stat.st_mode)){
				if(this->file_cache.count(clean_route) &&
				route_stat.st_mtime != this->file_cache[clean_route]->modified){
					this->file_cache.erase(clean_route);
				}
				if(this->file_cache.count(clean_route)){
					// Send the file from the cache.
					CachedFile* cached_file = file_cache[clean_route];

					if(Util::endsWith(clean_route, ".css")){
						response = response_header + "Content-Type: text/css\n";
					}else if(Util::endsWith(clean_route, ".html")){
						response = response_header + "Content-Type: text/html\n";
					}else if(Util::endsWith(clean_route, ".svg")){
						response = response_header + "Content-Type: image/svg+xml\n";
					}else{
						response = response_header + "Content-Type: text/plain\n";
					}
					response = response + "Content-Length: " + std::to_string(cached_file->data_length) + "\r\n\r\n";
					if(this->server->send(fd, response.c_str(), response.length())){
						return -1;
					}
					//DEBUG("DELI:" << response)
					if(r_obj.GetStr("method") != "HEAD"){
						size_t buffer_size = BUFFER_LIMIT;
						size_t offset = 0;
						while(buffer_size == BUFFER_LIMIT){
							if(offset + buffer_size > cached_file->data_length){
								// Send final bytes.
								buffer_size = cached_file->data_length % BUFFER_LIMIT;
							}
							if(this->server->send(fd, cached_file->data + offset, buffer_size)){
								return -1;
							}
							offset += buffer_size;
						}
					}
					PRINT("Cached file served: " << clean_route)
				}else{
					if(Util::endsWith(clean_route, ".css")){
						response = response_header + "Content-Type: text/css\n";
					}else if(Util::endsWith(clean_route, ".html")){
						response = response_header + "Content-Type: text/html\n";
					}else if(Util::endsWith(clean_route, ".svg")){
						response = response_header + "Content-Type: image/svg+xml\n";
					}else{
						response = response_header + "Content-Type: text/plain\n";
					}
					response = response + "Content-Length: " + std::to_string(route_stat.st_size) + "\r\n\r\n";
					if(this->server->send(fd, response.c_str(), response.length())){
						return -1;
					}
					//DEBUG("DELI:" << response)

					if(r_obj.GetStr("method") != "HEAD"){
						if(this->file_cache_remaining_bytes > route_stat.st_size && this->file_cache_mutex.try_lock()){
							// Stick the file into the cache AND send it
							CachedFile* cached_file = new CachedFile();
							cached_file->data_length = static_cast<size_t>(route_stat.st_size);
							DEBUG("Caching " << clean_route << " for " << route_stat.st_size << " bytes.")
							cached_file->data = new char[route_stat.st_size];
							cached_file->modified = route_stat.st_mtime;
							int offset = 0;
							this->file_cache[clean_route] = cached_file;
							this->file_cache_remaining_bytes -= cached_file->data_length;

							int file_fd;
							ssize_t len;
							char buffer[BUFFER_LIMIT + 32];
							if((file_fd = open(clean_route.c_str(), O_RDONLY | O_NOFOLLOW)) < 0){
								perror("open file");
								this->file_cache_mutex.unlock();
								return 0;
							}
							while(file_fd > 0){
								if((len = read(file_fd, buffer, BUFFER_LIMIT)) < 0){
									perror("read file");
								this->file_cache_mutex.unlock();
									return 0;
								}
								buffer[len] = 0;
								// Only different line (copy into memory)
								std::memcpy(cached_file->data + offset, buffer, static_cast<size_t>(len));
								offset += len;
								if(this->server->send(fd, buffer, static_cast<size_t>(len))){
									this->file_cache_mutex.unlock();
									return -1;
								}
								if(len < BUFFER_LIMIT){
									break;
								}
							}
							if(close(file_fd) < 0){
								perror("close file");
								this->file_cache_mutex.unlock();
								return 0;
							}
							PRINT("File cached and served: " << clean_route)
							this->file_cache_mutex.unlock();
						}else{
							// Send the file read-buffer style
							int file_fd;
							ssize_t len;
							char buffer[BUFFER_LIMIT + 32];
							if((file_fd = open(clean_route.c_str(), O_RDONLY | O_NOFOLLOW)) < 0){
								perror("open file");
								return 0;
							}
							while(file_fd > 0){
								if((len = read(file_fd, buffer, BUFFER_LIMIT)) < 0){
									perror("read file");
									return 0;
								}
								buffer[len] = 0;
								if(this->server->send(fd, buffer, static_cast<size_t>(len))){
									return -1;
								}
								if(len < BUFFER_LIMIT){
									break;
								}
							}
							if(close(file_fd) < 0){
								perror("close file");
								return 0;
							}
							PRINT("File served: " << clean_route)
						}
					}
				}
			}else{
				PRINT("Something other than a regular file was requested...")
				response_body = HTTP_404;
				response_header = response_header.replace(9, 6, "404 Not Found");
			}
		}else{
			if(route.length() >= 4 &&
			!Util::strict_compare_inequal(route.c_str(), "/api", 4)){
				route = r_obj.GetStr("method") + ' ' + route;
				if(route[route.length() - 1] != '/'){
					route += '/';
				}
			}

			PRINT((r_type == API ? "APIR: " : "HTTPAPIR: ") << route)

			if(this->routemap.count(route)){
				if(this->routemap[route]->minimum_ms_between_call.count() > 0){
					std::chrono::milliseconds now = std::chrono::duration_cast<std::chrono::milliseconds>(
						std::chrono::system_clock::now().time_since_epoch());

					if(this->routemap[route]->client_ms_at_call.count(this->server->fd_to_details_map[fd])){
						std::chrono::milliseconds diff = now -
						this->routemap[route]->client_ms_at_call[this->server->fd_to_details_map[fd]];

						if(diff < this->routemap[route]->minimum_ms_between_call){
							DEBUG("DIFF: " << diff.count() << "\nMINIMUM: " << this->routemap[route]->minimum_ms_between_call.count())
							response_body = "{\"error\":\"This API route is rate-limited.\"}";
						}else{
							this->routemap[route]->client_ms_at_call[this->server->fd_to_details_map[fd]] = now;
						}
					}else{
						this->routemap[route]->client_ms_at_call[this->server->fd_to_details_map[fd]] = now;
					}
				}

				if(response_body.empty()){
					for(auto iter = this->routemap[route]->requires.begin(); iter != this->routemap[route]->requires.end(); ++iter){
						if(!r_obj.HasObj(iter->first, iter->second)){
							response_body = "{\"error\":\"'" + iter->first + "' requires a " + JsonObject::typeString[iter->second] + ".\"}";
							break;
						}else{
							DEBUG("\t" << iter->first << ": " << r_obj.objectValues[iter->first]->stringify(true))
						}
					}
				}

				if(response_body.empty()){
					if(this->routemap[route]->requires_human){
						if(!r_obj.HasObj("answer", STRING)){
							response_body = "{\"error\":\"You need to answer the question: " + client_questions[fd]->q + "\"}";
						}else{
							for(auto sa : client_questions[fd]->a){
								if(r_obj.GetStr("answer") == sa){
									response_body = this->routemap[route]->function(&r_obj);
									client_questions[fd] = get_question();
									break;
								}
							}
							if(response_body.empty()){
								response_body = "{\"error\":\"You provided an incorrect answer.\"}";
							}
						}
					}else{
						if(this->routemap[route]->function != nullptr){
							response_body = this->routemap[route]->function(&r_obj);
						}else if(this->routemap[route]->token_function != nullptr){
							if(!r_obj.HasObj("token", STRING)){
								response_body = "{\"error\":\"'token' requires a string.\"}";
							}else{
								JsonObject* token = new JsonObject();
								try{
									token->parse(this->encryptor->decrypt(JsonObject::deescape(r_obj.GetStr("token"))).c_str());
									response_body = this->routemap[route]->token_function(&r_obj, token);
								}catch(const std::exception& e){
									DEBUG(e.what())
									response_body = INSUFFICIENT_ACCESS;
								}
							}
						}else{
							if(this->routemap[route]->raw_function(&r_obj, fd) <= 0){
								PRINT("RAW FUNCTION BAD")
								return -1;
							}
							r_type = JSON;
						}
						if(response_body.empty()){
							response_body = "{\"error\":\"The data could not be acquired.\"}";
						}
					}
				}
			}else if(route == "GET /api/question/"){
				response_body = "{\"result\":\"Human verification question: " + client_questions[fd]->q + "\"}";
			}else{
				PRINT("BAD ROUTE: " + route)
				response_body = "{\"error\":\"Invalid API route.\"}";
			}
		}
		
		if(!response_body.empty() && r_type != JSON){
			if(r_type == API){
				if(this->server->send(fd, response_body.c_str(), response_body.length())){
					return -1;
				}
			}else{
				if(r_type == HTTP){
					if(Util::endsWith(route, ".css")){
						response = response_header + "Content-Type: text/css\n";
					}else if(Util::endsWith(route, ".svg")){
						response = response_header + "Content-Type: image/svg+xml\n";
					}else{
						response = response_header + "Content-Type: text/html\n";
					}
				}else{
					response = response_header + "Content-Type: application/json\n";
				}
				response = response + "Content-Length: " + std::to_string(response_body.length()) + "\r\n\r\n" + response_body;
				if(this->server->send(fd, response.c_str(), response.length())){
					return -1;
				}
			}
		}
		
		return data_length;
	};

	// Now run forever.

	// As many threads as possible.
	this->server->run();
	
	// Single threaded.
	//this->server->run(false, 1);

	PRINT("Goodbye!")
}

void HttpApi::set_file_cache_size(int megabytes){
	this->file_cache_remaining_bytes = megabytes * 1024 * 1024;
}

HttpApi::~HttpApi(){
	delete this->routes_object;
	for(auto iter = this->routemap.begin(); iter != this->routemap.end(); ++iter){
		delete iter->second;
	}
	for(auto iter = this->file_cache.begin(); iter != this->file_cache.end(); ++iter){
		delete[] iter->second->data;
		delete iter->second;
	}
	DEBUG("API DELETED")
}

