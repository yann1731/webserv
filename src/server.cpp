#include "../include/server.hpp"
#include <exception>
#include <string>
#include "../include/cgi.hpp"

extern HttpConfig httpConfig;

HttpServer::HttpServer(HttpConfig httpConfig, SocketGenerator socket_generator)
    : socket_generator_(socket_generator), config_(httpConfig) {}

HttpServer::~HttpServer() {}

void HttpServer::start(bool run_server) {
    Logger::instance().log("Starting server");

    // Set up signal handlers
    listener_.registerEvent(SIGINT, SIGNAL_EVENT);
    listener_.registerEvent(SIGTERM, SIGNAL_EVENT);

    // Create a socket for each server in the config
    Socket *new_socket;
    for (std::vector<ServerConfig>::iterator it = config_.servers.begin();
         it != config_.servers.end(); ++it) {
        try {
            Logger::instance().log("Creating socket for server: http://" + it->listen.first + ":" +
                                   std::to_string(it->listen.second));

            // Create a new socket
            new_socket = socket_generator_();

            // Bind the socket to the address/port
            int server_id = new_socket->bind(it->listen.first, it->listen.second);

            // Listen for connections
            new_socket->listen();

            // Add the socket to the map
            server_sockets_[server_id] = new_socket;

            // Add the socket to the listener
            listener_.registerEvent(server_id, READABLE);

        } catch (std::bad_alloc &e) {
            Logger::instance().log(e.what());
        } catch (std::exception &e) {
            Logger::instance().log(e.what());
            delete new_socket;
        }
    }

    // Run the server
    if (run_server == true) run();
}

bool HttpServer::stop() {
    Logger::instance().log("Stopping server");

    // Close all sockets and delete them
    for (std::map<int, Socket *>::iterator it = server_sockets_.begin();
         it != server_sockets_.end(); ++it) {
        try {
            it->second->close();
        } catch (std::runtime_error &e) {
            Logger::instance().log(e.what());
        }
        delete it->second;
    }

    // Clear the map of sockets
    std::map<int, Session*>::iterator it = sessions_.begin();
    if (it != sessions_.end()) {
        for (std::map<int, Session*>::iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
            delete it->second;
        }
    }
    sessions_.clear();
    server_sockets_.clear();
    return true;
}

void HttpServer::run() {
    Logger::instance().log("Running server");

    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);

    // Loop forever
    while (true) {
        // Wait for an event
        std::pair<int, InternalEvent> event = listener_.listen();

        // Handle event
        if (server_sockets_.find(event.first) != server_sockets_.end()) {
            connectHandler(event.first);
        } else {
            switch (event.second) {
                case NONE:
                    break;
                case READABLE:
                    readableHandler(event.first);
                    break;
                case WRITABLE:
                    writableHandler(event.first);
                    break;
                case ERROR_EVENT:
                    errorHandler(event.first);
                    break;
                case SIGNAL_EVENT:
                    if (signalHandler(event.first))
                        return;
            }
        }
    }
}

void HttpServer::readableHandler(int session_id) {
    // Logger::instance().log("Received request on fd: " + std::to_string(session_id));

    // Receive the request
    try {
        std::pair<std::string, ssize_t> partialRequest = receiveRequestChunk(session_id); //Should prolly chunk the request instead
        if (partialRequest.second == 0 || partialRequest.second < READ_BUFFER_SIZE) {
            sessions_[session_id]->appendToRawRequest(partialRequest.first);
            HttpRequest request = HttpRequest(sessions_[session_id]->getRawRequest(), sessions_[session_id]);
            HttpResponse response = handleRequest(request);
            sessions_[session_id]->addSendQueue(response.getMessage());
            listener_.registerEvent(session_id, WRITABLE);   
        }
        else {
            sessions_[session_id]->appendToRawRequest(partialRequest.first);
        }
    } catch (std::exception &e) {
        disconnectHandler(session_id);
        std::cerr << e.what() << std::endl;
    }
    // Logger::instance().log(request.first.printRequest());
}

void HttpServer::writableHandler(int session_id) {
    if (sessions_[session_id]->send()) {
        // listener_.unregisterEvent(session_id, WRITABLE);
        disconnectHandler(session_id);
    }
}

void HttpServer::errorHandler(int session_id) {
    Logger::instance().log("Error on fd: " + std::to_string(session_id));
    return;
}

bool HttpServer::signalHandler(int signal) {
    // Logger::instance().log("Received signal: " + std::to_string(signal));

    if (signal == SIGINT || signal == SIGTERM) {
        return stop();
    }
    return false;
}

void HttpServer::connectHandler(int socket_id) {
    // Logger::instance().log("Received connection on fd: " + std::to_string(socket_id));

    // Accept the connection
    Session *session = server_sockets_[socket_id]->accept();

    // Create a new session
    sessions_[session->getSockFd()] = session;

    // Add the session to the listener
    listener_.registerEvent(session->getSockFd(), READABLE); /** @todo event flags */
}

void HttpServer::disconnectHandler(int session_id) {
    // Logger::instance().log("Disconnecting fd: " + std::to_string(session_id));

    // Remove the session from the listener
    listener_.unregisterEvent(session_id, READABLE | WRITABLE);

    listener_.removeEvent(session_id);

    // Delete the session
    delete sessions_[session_id];

    // Remove the session from the map
    sessions_.erase(session_id);

    // Close the socket
    close(session_id);
}

std::pair<std::string, ssize_t> HttpServer::receiveRequestChunk(int session_id) {
    std::pair<std::string, ssize_t> buffer_pair = sessions_[session_id]->recv(session_id);
    return buffer_pair;
}

bool isResourceRequest(HttpResponse &response, const std::string &uri) {
    if (uri.size() >= 4 && uri.substr(uri.size() - 4) == ".css") {
        response.headers_["Content-Type"] = "text/css";
        return true;
    }
    if (uri.size() >= 3 && uri.substr(uri.size() - 3) == ".js") {
        response.headers_["Content-Type"] = "text/javascript";
        return true;
    }
    if (uri.size() >= 4 && uri.substr(uri.size() - 4) == ".pdf") {
        response.headers_["Content-Type"] = "application/pdf";
        return true;
    }
    return false;
}

// Build the error page : ToDo -> Make it take the error code
bool HttpServer::buildErrorPage(HttpRequest &request, HttpResponse &response, ServerConfig &server,
                               LocationConfig *location, HttpStatus status) {
    response.status_ = status;
    std::string root = location && location->root.size() > 0 ? location->root : server.root;
    if (isResourceRequest(response, request.uri_)) {
        std::string resource = root + request.uri_;
        response.status_ = OK;
        return readFileToBody(response, resource, location);
    }
    std::string *errorPath = NULL;
    if (location) {
        std::map<int, std::string>::iterator it = location->error_page.find(status);
        if (it != location->error_page.end())
            errorPath = &it->second;
    }
    if (!errorPath) {
        std::map<int, std::string>::iterator it = server.error_page.find(status);
        if (it != server.error_page.end())
            errorPath = &it->second;
    }
    if (!errorPath) {
        std::map<int, std::string>::iterator it = config_.error_page.find(status);
        if (it != config_.error_page.end())
            errorPath = &it->second; 
    }
    if (!errorPath)
        return false;
    std::string filepath = root + '/' + *errorPath;
    return readFileToBody(response, filepath, location);
}

// Read a file into the response body
bool HttpServer::readFileToBody(HttpResponse &response, std::string &filepath, LocationConfig *location) {
    if (!location)
        return false;
    std::ifstream in(filepath + location->index_file);
    if (!in) {
        in.open(filepath);
        if (!in) {
            return false;
        }
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    response.body_ = buffer.str();
    in.close();
    return true;
}

std::string HttpServer::trimHost(const std::string &uri, ServerConfig &server) {
    size_t startPos = uri.find(std::to_string(server.listen.second));
    if (startPos != std::string::npos) {
        startPos += std::to_string(server.listen.second).size();
        return uri.substr(startPos);
    }
    return uri;
}

std::string HttpServer::getUploadDirectory(ServerConfig &server, LocationConfig *location) {
    if (!location->upload_dir.empty()) {
        return location->upload_dir;
    } else if (!server.upload_dir.empty()) {
        return server.upload_dir;
    } else {
        return config_.upload_dir;
    }
} 

bool HttpServer::deleteFile(ServerConfig &server, LocationConfig *location, const std::string &filename) {
    std::string base_path = getUploadDirectory(server, location);
    std::string filePath = base_path + "/" + filename;
    return std::remove(filePath.c_str()) == 0;
}

void HttpServer::uploadsFileList(ServerConfig &server, LocationConfig *location, std::stringstream &fileList) {
    fileList << "<script>"
         << "function handleDeleteButtonClick(filename) {"
         << "    var currentUrl = window.location.href + 'delete?filename=' + encodeURIComponent(filename);"
         << "    fetch(currentUrl, {"
         << "        method: 'DELETE',"
         << "        headers: { 'Content-Type': 'application/json' },"
         << "    })"
         << "    .then(response => {"
         << "        if (response.ok) {"
         << "            console.log('DELETE request for ' + filename + ' successful');"
         << "        } else {"
         << "            console.error('DELETE request for ' + filename + ' failed');"
         << "        }"
         << "    })"
         << "    .catch(error => {"
         << "        console.error('Error during DELETE request for ' + filename + ':', error);"
         << "    });"
         << "}"
         << "</script>";
    
    DIR *dir;
    struct dirent *ent;
    std::string dir_path = getUploadDirectory(server, location);
    if ((dir = opendir(dir_path.c_str())) != NULL) {
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] != '.') {
                std::string filename = std::string(ent->d_name);
                fileList << "<li style=\"clear: both;\">"
                         << "<div style=\"display: inline;\">" << filename << "</div>"
                         << "<form id=\"deleteForm" << filename << "\" style=\"float: right;\">"
                         << "<input type=\"hidden\" name=\"filename\" value=\"" << filename << "\">"
                         << "<input type=\"button\" value=\"Delete\" onclick=\"handleDeleteButtonClick('" << filename << "');\">"
                         << "</form>"
                         << "</li>";
            }
        }
        closedir(dir);
    }
}

std::string urlDecode(const std::string& str) {
    std::stringstream decoded;
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '%' && i + 2 < str.length()) {
            char decodedChar = static_cast<char>(std::strtoul(str.substr(i + 1, 2).c_str(), nullptr, 16));
            decoded << decodedChar;
            i += 2;
        } else if (str[i] == '+') {
            decoded << ' ';
        } else {
            decoded << str[i];
        }
    }
    return decoded.str();
}

bool HttpServer::deleteMethod(HttpRequest &request, HttpResponse &response,
                           ServerConfig &server, LocationConfig *location) {
    (void) server;
    (void) location;
    
    size_t position = request.uri_.find("=");
    std::string encodedFilename = request.uri_.substr(position + 1);
    std::string filename = urlDecode(encodedFilename);
    deleteFile(server, location, filename);
    std::stringstream fileList;
    uploadsFileList(server, location, fileList);
    response.body_ = "<html><body><h2>Uploads:</h2><ul>" + fileList.str() + "</ul>" + "<a href='/'>Return Home</a></body></html>";
    response.status_ = OK;
    return true;
}

std::string extractValue(const std::string& data, const std::string& start, const std::string& end) {
    size_t startPos = data.find(start) + start.length();
    size_t endPos = data.find(end, startPos);
    if (end.empty()) {
        endPos = data.length();
    }
    return data.substr(startPos, endPos - startPos);
}

bool fileExists(const std::string &filePath) {
    std::ifstream file(filePath.c_str());
    return file.good();
}

std::string HttpServer::generateUniqueFileName(ServerConfig &server, LocationConfig *location, std::string &originalFileName) {
    std::string base_path = getUploadDirectory(server, location) + "/";
    std::string newFileName = base_path + originalFileName;
    std::string nameWithoutExtension;
    size_t dot_position;
    bool moreThanOneChange = false;
    int counter = 1;
    while(fileExists(newFileName)) {
        ++counter;
        if (counter > 2) {
            moreThanOneChange = true;
        }
        dot_position = newFileName.find('.');
        nameWithoutExtension = newFileName.substr(0, dot_position);
        if (moreThanOneChange == true) {
            dot_position = originalFileName.find('.'); 
            newFileName = base_path + originalFileName.substr(0, dot_position) + "_" + std::to_string(counter) + originalFileName.substr(dot_position);
        }
        else
            newFileName = nameWithoutExtension + "_" + std::to_string(counter) + newFileName.substr(dot_position);
    }
    return (newFileName);
}

std::string readFileContent(const std::string& filename) {
    std::ifstream file(filename.c_str(), std::ios::binary);
    if (file) {
        std::stringstream content;
        content << file.rdbuf();
        file.close();
        return content.str();
    }
    else {
        return "Error reading file: " + filename;
    }
}

bool HttpServer::displayFile(HttpRequest& request, HttpResponse& response, ServerConfig &server, LocationConfig *location) {
    std::string filename;
    std::string base_path = getUploadDirectory(server, location) + "/";
    size_t pos = request.uri_.find('?');
    if (pos != std::string::npos) {
        filename = request.uri_.substr(pos + 1);
    }

    std::string filePath = base_path + filename;

    std::ifstream file(filePath.c_str());
    if (!file.is_open()) {
        return buildErrorPage(request, response, server, location, NOT_FOUND);
    }
    file.close();

    std::string fileContent = readFileContent(filePath);

    response.headers_["Content-Type"] = "application/octet-stream";
    response.headers_["Content-Disposition"] = "attachment; filename=\"" + filename + "\"";

    response.body_ = fileContent;

    response.status_ = OK;
    response.headers_["Content-Length"] = std::to_string(response.body_.size());

    return true;
}

bool HttpServer::postMethod(HttpRequest &request, HttpResponse &response, ServerConfig &server,
                            LocationConfig *location) {
    (void)server;
    (void)location;

    if (request.body_.empty()) {
        return buildErrorPage(request, response, server, location, BAD_REQUEST);
    } else {
        std::string userInput;
        size_t pos = request.body_.find("text_input=");
        if (pos != std::string::npos) {
            userInput = request.body_.substr(pos + 11);
            if (userInput.empty()) {
                response.body_ = "<html><body>This field cannot be empty<br><br><a href='/'>Return Home</a></body></html>";
                return true;
            }
            else {
                response.body_ = "<html><body>You've entered: " + userInput + "<br><br><a href='/'>Return Home</a></body></html>";
                return true;
            }
        }
        response.status_ = OK;
        response.headers_["Content-Type"] = "text/html";
        response.headers_["Content-Length"] = std::to_string(response.body_.size());
    }

    if (request.headers_["Content-Type"] == "text/plain") {
        response.headers_["Content-Type"] = "text/plain; charset=utf-8";

        if (true) {
            response.status_ = CREATED;
            response.body_   = "File created successfully";
        } else {
            return buildErrorPage(request, response, server, location, INTERNAL_SERVER_ERROR);
        }

    } else if (request.headers_["Content-Type"].find("multipart/form-data") != std::string::npos) {

        std::string boundary = extractValue(request.headers_["Content-Type"], "boundary=", "");
        if (request.body_.empty()) {
            std::cerr << "Problem uploading the file" << std::endl;
            response.body_ = "<html><body>There was an error uploading the file<br><br><a href='/'>Return Home</a></body></html>";
            return true;
        }

        std::string delimiter = "--" + boundary;
        size_t pos = 0;
        pos = request.body_.find(delimiter);
        while (pos != std::string::npos) {
            size_t endPos = request.body_.find(delimiter, pos + delimiter.length());
            
            std::string part = request.body_.substr(pos, endPos - pos);

            size_t filenamePos = part.find("filename=\"");
            if (filenamePos != std::string::npos) {
                std::string filename = extractValue(part, "filename=\"", "\"");
                if (filename.empty()) {
                    break;
                }
                std::string realFileName = generateUniqueFileName(server, location, filename);
                size_t contentPos = part.find("\r\n\r\n") + 4;
                std::ofstream file(realFileName.c_str(), std::ios::binary | std::ios::app);
                std::string content = part.substr(contentPos, part.length() - contentPos - 2);
                file << content;
                file.close();                
            }
            pos = response.body_.find(delimiter, endPos);
        }
    }
    else if (request.headers_["Content-Type"] == "application/x-www-form-urlencoded") {
        response.headers_["Content-Type"] = "text/html; charset=utf-8";

        if (true) {
            response.status_ = OK;

        } else {
            return buildErrorPage(request, response, server, location, INTERNAL_SERVER_ERROR);
        }
    } else {
        return buildErrorPage(request, response, server, location, BAD_REQUEST);
    }

    std::stringstream fileList;
    uploadsFileList(server, location, fileList);
    
    response.body_ = "<html><body><h2>Uploads:</h2><ul>" + fileList.str() + "</ul>" + "<a href='/'>Return Home</a></body></html>";
    response.status_ = OK;
    response.headers_["Content-Type"] = "text/html";
    response.headers_["Content-Length"] = std::to_string(response.body_.size());

    return true;
}

bool HttpServer::getMethod(HttpRequest &request, HttpResponse &response,
                           ServerConfig &server, LocationConfig *location) {
    response.headers_["Content-Type"] = "text/html; charset=utf-8";
    if (location) {     
        if (!isResourceRequest(response, request.uri_) && location->autoindex)
            request.uri_ = request.uri_ + location->index_file;
        std::string root = location->root.length() > 0 ? location->root : server.root;
        std::string filepath = isResourceRequest(response, request.uri_) ? request.uri_ : root + request.uri_;
        if (readFileToBody(response, filepath, location)) {
            response.status_ = OK;
            return true;
        }
    }
    return buildErrorPage(request, response, server, location, NOT_FOUND);
}

bool HttpServer::validateRequestBody(HttpRequest &request, ServerConfig &server, LocationConfig *location) {
    size_t max = location->client_max_body_size;
    if (location->max_body_size) {
        max = location->client_max_body_size;
    } else if (server.max_body_size) {
        max = server.client_max_body_size;
    } else if (this->config_.max_body_size) {
        max = this->config_.client_max_body_size;
    }
    return request.body_.size() <= max;
}

bool HttpServer::isRedirect(HttpRequest &request, HttpResponse &response, std::pair<int, std::string> &redirect) {
    if (redirect.first == 0)
        return false;
    response.status_ = HttpStatus(redirect.first);
    size_t pos = redirect.second.find("$request_uri");
    if (pos != std::string::npos) {
        response.headers_["Location"] = redirect.second.substr(0, pos) + request.uri_;
    } else {
        response.headers_["Location"] = redirect.second;
    }
    response.headers_["Content-Length"] = std::to_string(response.body_.size());
    return true;
}

// Find the appropriate location and fill the response body
bool HttpServer::buildResponse(HttpRequest &request, HttpResponse &response,
                           ServerConfig &server) {
    LocationConfig *location = NULL;
    if (isRedirect(request, response, server.redirect)) {
        return true;
    }
    std::string uri = isResourceRequest(response, request.uri_) ? trimHost(request.headers_["Referer"], server) : request.uri_;
    for (std::map<std::string, LocationConfig>::iterator it = server.locations.begin();
     it != server.locations.end(); ++it) {    
        if (uri.substr(0, it->first.size()) == it->first) {
            location = &(it->second);
        }
    }
    if (!location) {
        return buildErrorPage(request, response, server, location, NOT_FOUND);
    } else if (isResourceRequest(response, request.uri_)) {
        return getMethod(request, response, server, location);
    } else if (isRedirect(request, response, location->redirect)) {
        return true;
    } else if (!(std::find(location->limit_except.begin(), location->limit_except.end(), static_cast<int>(request.method_)) != location->limit_except.end())) {
        return buildErrorPage(request, response, server, location, METHOD_NOT_ALLOWED);
    } else if (!validateRequestBody(request, server, location)) {
        return buildErrorPage(request, response, server, location, CONTENT_TOO_LARGE);
    }
    if (checkIfDirectoryRequest(request, location, server) && request.method_ == GET) {
        if (checkForIndexFile(request, location, server)) {
            handleIndexFile(request, response, location, server);
        }
        else if (location->autoindex) {
            generateDirectoryListing(request, response, location, server);
        }
        else { //if autoindex is disabled and the request is for a directory by default server will return an error 403
            return buildErrorPage(request, response, server, location, FORBIDDEN);
        }
        return true;
    }
    else if (location->cgi_enabled && checkUriForExtension(request.uri_, location)) { //cgi handling before. Unsure if it should stay here or be handle within getMethod or postMethod
        Cgi newCgi(request, *location, server, response, config_);
        return newCgi.exec();
    }
    else {
        switch (request.method_) {
        case 1: // Enums for comparisons is C++11...
            return getMethod(request, response, server, location);
        case 2: // Enums for comparisons is C++11...
            return postMethod(request, response, server, location);
        case 3: // Enums for comparisons is C++11...
            return deleteMethod(request, response, server, location);
        default:
            return false;
        }
    }
}

// Validate the host making the request is in the servers
bool HttpServer::validateHost(HttpRequest &request, HttpResponse &response) {
    std::string requestHost = request.headers_["Host"];  // Check if the host is valid

    for (std::vector<ServerConfig>::iterator it = config_.servers.begin();
         it != config_.servers.end(); ++it) {
        std::string serverHost = it->listen.first + ":" + std::to_string(it->listen.second);
        std::string localHost = "localhost:" + std::to_string(it->listen.second);
        if (requestHost == serverHost || requestHost == localHost) {
            return buildResponse(request, response, *it);
        }
        for (std::vector<std::string>::iterator server_name = (*it).server_names.begin();
                server_name != (*it).server_names.end(); ++server_name) {
                    if (requestHost == *server_name + ":" + std::to_string(it->listen.second))
                        return buildResponse(request, response, *it);
                }
    }
    return false;
}

HttpResponse HttpServer::handleRequest(HttpRequest request) {
    HttpResponse response;
    
    response.version_ = HTTP_VERSION;
    response.server_  = "webserv/0.1";
    response.headers_["Connection"] = "Keep-Alive";

    if (request.version_ != "HTTP/1.1") {
        response.status_ = IM_A_TEAPOT; // If this happen we ignore the request and return an empty answer
    } else if (!validateHost(request, response)) {
        response.status_                  = NOT_FOUND;
        response.headers_["Content-Type"] = "text/html";
        response.body_ = "<html><head><style>body{display:flex;justify-content:center;align-items:center;height:100vh;margin:0;}.error-message{text-align:center;}</style></head><body><div class=\"error-message\"><h1>Homemade Webserv</h1><h1>404 Not Found</h1></div></body></html>";
    }
    if (response.body_.size() > 0) {
        response.headers_["content-length"] = std::to_string(response.body_.size());
    }
    return response;
}

bool HttpServer::checkUriForExtension(std::string& uri, LocationConfig *location) const {
    std::string ext;

	for (size_t i = 0; i < location->cgi_ext.size(); ++i) {
		if (uri.find(location->cgi_ext[i]) != std::string::npos) {
			ext = location->cgi_ext[i];
		}
	}
	if (!ext.size())
		return false;
    return true;
}

std::string HttpServer::findRoot(LocationConfig *location, ServerConfig &server) {
    if (location && location->root.size()) {
        return location->root;
    } else if (server.root.size()) {
        return server.root;
    } else if (config_.root.size()) {
        return config_.root;
    } else {
        return "";
    }
}

void HttpServer::handleIndexFile(HttpRequest &request, HttpResponse &response, LocationConfig *location, ServerConfig &server) {
    try
    {
        std::string tempUri = findRoot(location, server);
        tempUri.append(request.uri_);
        if (tempUri.back() != '/')
            tempUri.append("/");
        tempUri.append(location->index_file);
        if (readFileToBody(response, tempUri, location) == true) {
            response.headers_["content-type"] = "text/html";
            response.status_ = OK;
        } else { //something went wrong with reading index.html file
            return ;
        }
    }
    catch(const std::exception& e)
    {
        Logger::instance().log("Error happened in handleIndexFile");
        std::cerr << e.what() << '\n';
        return ;
    }
}

bool HttpServer::checkIfDirectoryRequest(HttpRequest &request, LocationConfig *location, ServerConfig &server) { //used to check if request is simply for a directory
    try
    {
        std::string tempUri = "";
        if (location->root.size()) { //check if root is set at the location level
            tempUri.append(location->root);
        } else if (server.root.size()) { //fallback to server root directive
            tempUri.append(server.root);
        } else if (config_.root.size()) {
            tempUri.append(config_.root);
        }
        if (request.uri_.find_last_of("/") != request.uri_.size() - 1)
            tempUri.append("/");
        tempUri.append(request.uri_);
        DIR *currentDirectory = opendir(tempUri.c_str()); //attempt to open the directory specified. If successful then it means the request was indeed for a directory.
        if (!currentDirectory) {
            return (false);
        }
        if (closedir(currentDirectory)) { //to prevent leaks. Cause leaks suck
            std::string error("Call to closedir failed: ");
            error.append(strerror(errno));
            Logger::instance().log(error);
        }
        return true;   
    }
    catch(const std::exception& e)
    {
        Logger::instance().log("Error occured in checkIfDirectory Listing");
        std::cerr << e.what() << '\n';
        return false;
    }
}

bool HttpServer::checkForIndexFile(HttpRequest &request, LocationConfig *location, ServerConfig &server) {
    try {
        std::vector<std::pair<unsigned char, std::string> > files = returnFiles(request, location, server);
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (files[i].second == location->index_file) { //checks the name of the file/folder
                if (files[i].first == DT_REG) { //checks if it is actually a file
                    return true; //yay a file called index.html exists!
                }
            }
        }
        return false; //got to generate an html document with the files contained within the requested directory
    }
    catch(const std::exception& e)
    {
        Logger::instance().log("Error happened in checkForIndexFile");
        std::cerr << e.what() << '\n';
        return false;
    }
}

void HttpServer::generateDirectoryListing(HttpRequest &request, HttpResponse &response, LocationConfig *location, ServerConfig &server) {
    std::string responseBody;
    std::vector<std::pair<unsigned char, std::string> > currentDir = returnFiles(request, location, server);
    std::vector<std::string> directories;
    std::vector<std::string> files;

    for (std::size_t i = 0; i < currentDir.size(); ++i) {
        if (currentDir[i].first == DT_DIR) {
            if (currentDir[i].second != ".")
                directories.push_back(currentDir[i].second);
        }
        if (currentDir[i].first == DT_REG) {
            files.push_back(currentDir[i].second);
        }
    }

    if (!hasTrailingSlash(request)) {
        std::string newLocation("http://");
        std::string host;
        std::map<std::string, std::string>::iterator it = request.headers_.find("Host");
        if (it != request.headers_.end()) {
            newLocation.append(it->second);
        }
        newLocation.append(request.uri_);
        newLocation.append("/");
        response.status_ = MOVED_PERMANENTLY;
        response.headers_["Location"] = newLocation;
    }
    else {
        response.status_ = OK;
    }
    response.headers_["content-type"] = "text/html";

    responseBody.append("<!doctype html><html><head><title>Index of ");
    responseBody.append(request.uri_);
    responseBody.append("</title></head><body><h1>Index of ");
    responseBody.append(request.uri_);
    responseBody.append("</h1><hr><pre>");
    for (std::size_t i = 0; i < directories.size(); ++i) {
        responseBody.append("<a href=\"");
        responseBody.append(directories[i]);
        responseBody.append("/\">");
        responseBody.append(directories[i]);
        responseBody.append("/</a>\n");
    }
    for (std::size_t i = 0; i < files.size(); ++i) {
        responseBody.append("<a href=\"");
        responseBody.append(files[i]);
        responseBody.append("\">");
        responseBody.append(files[i]);
        responseBody.append("</a>\n");
    }
    responseBody.append("</pre><hr></body></html>");
    response.body_ = responseBody;
}

std::vector<std::pair<unsigned char, std::string> > HttpServer::returnFiles(HttpRequest &request, LocationConfig *location, ServerConfig &server) {
    std::string tempUri = findRoot(location, server);
    std::vector<std::pair<unsigned char, std::string> > files;
    struct dirent *file;

    if (request.uri_.find_last_of("/") != request.uri_.size() - 1)
            tempUri.append("/");
    tempUri.append(request.uri_);
    DIR *currentDirectory = opendir(tempUri.c_str());
    do {
        file = readdir(currentDirectory);
        if (file != nullptr) {
            files.push_back(std::make_pair(file->d_type, file->d_name));
        }
    }
    while (file != nullptr);
    if (closedir(currentDirectory)) {
        std::string error("Something went wrong with closedir: ");
        error.append(strerror(errno));
        Logger::instance().log(error);
    }
    return files;
}

bool HttpServer::hasTrailingSlash(HttpRequest &request) const {
    if (request.uri_[request.uri_.size() - 1] == '/') {
        return true;
    }
    return false;
}

void HttpServer::addTrailingSlash(HttpRequest &request, HttpResponse &response) {
    std::string newUri = request.uri_;
    newUri.append("/");
    response.headers_["Location"] = newUri;
    response.status_ = MOVED_PERMANENTLY;
}