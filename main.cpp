#include <iostream>
#include <map>
#include <chrono>
#include <string>
#include <ctime>
#include <fstream>
#include <cstring>
#include <filesystem>

using namespace std;
using std::chrono::high_resolution_clock;
using std::chrono::duration_cast;
using std::chrono::duration;
using std::chrono::milliseconds;

extern "C"{
    #include "fake_receiver.h"
}

//const for starting end ending session
const string START_1 = "0A0#6601";
const string START_2 = "0A0#FF01";
const string STOP_1 = "0A0#66FF";

//const for id and payload size
const int MAX_SIZE_ID = 3;
const int MAX_SIZE_PAYLOAD = 16;

//const for buffers size
const int MAX_WRITE_MSG_SIZE = 200;
const int MAX_PATH_SIZE= 500;

//these are the possible states
//TODO use START_PARSE state to parse the messages
enum STATUS{IDLE, START, START_PARSE};

//counter for csv files
int counter_csv_files = 1;

/**
 * find the absolute path of a file
 * @param file the file with a relative ppath
 * @return a string of the absolute path of the file
 */
char* getAbsolutePath(const char *file);

/**
 * create a unique string to use it as name for a file using date and time system
 * @return the string created
 */
string createNameFile();

/**
 * check if the message read is a start message
 * @param line the line read
 * @return true if is a start message, false otherwise
 */
bool check_start_message(string line);

/**
 * check if the message read is a stop message
 * @param line the line read
 * @return true if is a stop message, false otherwise
 */
bool check_stop_message(string line);

/**
 * parse the id message from exa base to deca base
 * @param line the message read
 * @return the deca value of the message in 12bit, -1 if some errors occur (eg. not even payload chars)
 */
int16_t parseId(string &line);

/**
 * parse the payload message from exa base to deca base
 * @param line the message read
 * @return the deca value of the message in 64bit, -1 if some errors occur (eg. not even payload chars)
 */
int64_t parsePayload(string &line);

/**
 * get the substring id from the message
 * @param line the message red
 * @return the id
 */
string getId(string line);

/**
 * get the substring payload from the message
 * @param line the message red
 * @return the payload
 */
string getPayload(string line);

/**
 * convert the exa string to decimal base value
 * @param hexVal the string
 * @return the decimal value in 64 bit
 */
int64_t hexadecimalToDecimal(string hexVal);

/**
 * create a csv file and save it in bin folder with name file_name
 * @param messages the map containing the values
 * @param file_name the name to give to the file
 * @return return 1 if some errors occurs
 */
int createCSV(auto &messages, const string& file_name);

/**
 * file log which saves every data with millis is located in
 * \bin\
 *
 * file csv which saves the data from start sessions is located in
 * \bin\
 *
 * the file used for starting the start interface is candump.log
 */

int main() {

    //------------------ opening CAN interface --------------------------------------

    char * path = getAbsolutePath("candump.log");
    if(open_can(path) == -1){
        //error in opening CAN interface
        cout << "Error opening CAN interface";
        return 1;
    }

    //log file for CAN interface
    string file_name = createNameFile();
    fstream logCAN;
    logCAN.open(file_name + ".log", ios::out);

    if (logCAN.fail()) {
        //error opening log file
        cout << "Error creating output log file for writing";
        return 1;
    }

    //reading messages
    char writeMs[MAX_WRITE_MSG_SIZE];//the message to be copied in the file
    char message[MAX_CAN_MESSAGE_SIZE];//the message read

    //variable to count the time between every message
    auto t1 = high_resolution_clock::now();

    //reading can interface
    while(can_receive(message) != -1){//error or EOF
        auto t2 = high_resolution_clock::now();//time after the message
        //write on log file the millis and the message received
        duration<double, std::milli> ms_double = (t2 - t1);//to get value, ms_double.count()

        strcpy(writeMs, to_string(ms_double.count()).c_str());
        strcat(writeMs, " ");
        strcat(writeMs, message);
        logCAN << writeMs << endl;
    }

    //closing CAN interface and log file
    logCAN.close();
    close_can();

    //-------- finish reading can interface -----------------------------------------

    //the program start at IDLE state
    STATUS state = IDLE;

    //reading the log file and do stats
    fstream readLog;
    readLog.open(file_name+".log", ios::in);

    if(readLog.fail()){
        //error opening the log file
        cout << "Error opening the log file for reading" << endl;
        return 1;
    }

    //struct to contain values about a message
    typedef struct{
        int nMsg;//number of times of the mesg
        double mean_time;
        double last_time;//indicates the last time to get a msg
    } values;

    //map containing stats about the id message
    map <string, values *> messages;

    double ms;//read millis from file

    char msg[MAX_WRITE_MSG_SIZE];//space needed to copy the string and the terminal char
    while(!readLog.eof()){
        //read per line millis and msg
        readLog >> ms >> msg;

        //-------------------- mod the status if needed -----------------------------------
        //check if the message is a start or a stop message
        if (check_start_message(msg)) {
            //change from IDLE to START if not already in START
            if (state == IDLE)
                state = START;


        } else if (check_stop_message(msg)) {
            //change from START to IDLE
            state = IDLE;

            //write csv file
            createCSV(messages, file_name);
            //end START status
        }
        //----------------------------------------------------------------------------

        //--------- writing data -----------------------------------------------------
        //if in start, saving values in map
        if (state == START) {
            //search for an existing id
            string id, payload;

            id = getId(msg);
            payload = getPayload(msg);

            //save the iterator of the search
            auto iterator = messages.find(id);

            //if there, increment counter and update mean
            //else, create new entry for key = ID
            if (iterator == messages.end()) {
                //create new struct for the current id
                auto *v = new values;
                v->nMsg = 1;
                v->mean_time = ms;
                v->last_time = ms;
                messages.insert(pair<string, values *>(id, v));

            } else {
                iterator->second->nMsg++;//update numbers of message with that id
                //update last_time with the last one read
                iterator->second->last_time = ms;
                //update mean
                double mean = iterator->second->mean_time;
                mean = (mean + ms) / iterator->second->nMsg;
                iterator->second->mean_time = mean;
            }

            //finish if START
        }

        //end while
    }

    if(state == START){
        //if we are still in START mode, export the data recorder in any case
        createCSV(messages, file_name);
    }

    //close log file
    readLog.close();

    delete path;

    return 0;
}

string createNameFile(){
    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer,80,"%d-%m-%Y %H-%M-%S",timeinfo);
    return string(buffer);
}

bool check_start_message(string line){
    return (line == START_1 || line == START_2);
}

bool check_stop_message(string line){
    return (line == STOP_1);
}

int16_t parseId(string &line){
    //split the message in two parts, id and payload
    string id = getId(line);

    if(id.size()<=MAX_SIZE_ID){
        //everything is ok
        return hexadecimalToDecimal(id);
    } else {
        //error of syntax
        return -1;
    }
}

int64_t parsePayload(string &line){
    string payload = getPayload(line);

    if(payload.size()<=MAX_SIZE_PAYLOAD && payload.size()%2==0){
        //everything ok
        return hexadecimalToDecimal(payload);
    } else {
        //error in syntax
        return -1;
    }
}

int64_t hexadecimalToDecimal(string hexVal){
    int len = hexVal.size();
    int base = 1;
    int64_t dec_val = 0;

    for (int i = len - 1; i >= 0; i--) {

        if (hexVal[i] >= '0' && hexVal[i] <= '9') {
            dec_val += (int(hexVal[i]) - 48) * base;

            base = base * 16;
        }

        else if (hexVal[i] >= 'A' && hexVal[i] <= 'F') {
            dec_val += (int(hexVal[i]) - 55) * base;
            base = base * 16;
        }
    }
    return dec_val;
}

string getId(string line){
    string sub;

    for (int i = 0; line[i] != '#'; i++)
        sub.push_back(line[i]);

    return sub;
}

string getPayload(string line){
    string sub;
    int index = 0;

    //to exclude the id part
    for(char c : line){
        index++;
        if(c == '#')
            break;
    }

    for(index; index<line.size(); index++){
        sub.push_back(line[index]);
    }

    return sub;
}

int createCSV(auto &messages, const string& file_name){
    fstream csv;
    string path = "";
    path.append(file_name);
    path.append("_");
    path.append(to_string(counter_csv_files));
    counter_csv_files++;//update che number of csv files created
    csv.open(path+".csv", ios::out);

    if(csv.fail()){
        cout << "Error creating csv file" << endl;
        return 1;
    }

    for (const auto &message: messages) {
        csv << message.first << "," << message.second->nMsg << "," << message.second->mean_time << "\n";
    }

    csv.close();
    messages.clear();//remove all data collected

    return 0;
}

char * getAbsolutePath(const char *file){
    auto path = std::filesystem::absolute(file);
    string pathStr = path.string();
    string subString = "\\bin";

    //remove the bin directory
    int start_position_to_erase = pathStr.find(subString);

    if(start_position_to_erase != string::npos)
        pathStr.erase(start_position_to_erase,subString.length());

    char *p = new char[MAX_PATH_SIZE];
    strcpy(p, pathStr.c_str());

    return p;
}