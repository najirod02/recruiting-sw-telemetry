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

//const for starting end ending sessions
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
enum STATUS{IDLE, RUN};

//counter for run files
int counter_run_files = 0;

/**
 * find the absolute path of a file
 * @param file the file with a relative ppath
 * @return a string of the absolute path of the file
 */
char * getAbsolutePath(const char *file);

/**
 * create a unique string to use it as name for a file using date and time system
 * @return the string created
 */
char * createNameFile();

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
int16_t parseId(const char * line);

/**
 * parse the payload message from exa base to deca base
 * @param line the message read
 * @return the deca values of the message of characters pairs, null if there's some errors
 */
short * parsePayload(const char * line);

/**
 * get the substring id from the message
 * @param line the message red
 * @return the id
 */
char * getId(const char * line);

/**
 * get the substring payload from the message
 * @param line the message red
 * @return the payload
 */
char * getPayload(const char * line);

/**
 * convert the exa string to decimal base value
 * @param hexVal the string
 * @return the decimal value
 */
int hexadecimalToDecimal(const char * hexVal);

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
    delete path;

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

    //writing data on the files
    fstream idleFile, runFile;

    idleFile.open(file_name+"_idle.log", ios::out);
    if(idleFile.fail()){
        //error opening the parse log file
        cout << "Error opening the idle file" << endl;
        return 1;
    }

    //struct to contain values about a message
    typedef struct{
        int nMsg;//number of times of the msg
        double total_time;
    } values;

    //map containing stats about the id message
    map <string, values *> messages;

    double ms;//read millis from file
    char msg[MAX_WRITE_MSG_SIZE];
    char * id, * payload;
    short * payloadParse;

    while(!readLog.eof()){
        //read per line millis and msg
        readLog >> ms >> msg;

        //update the map with the message read -----------------------------
        id = getId(msg);
        payload = getPayload(msg);

        auto iterator = messages.find(id);
        //if there, increment counter and update mean
        //else, create new entry for key = ID
        if (iterator == messages.end()) {
            //create new struct for the current id
            auto *v = new values;
            v->nMsg = 1;
            v->total_time = ms;
            messages.insert(pair<string, values *>(id, v));

        } else {
            iterator->second->nMsg++;//update numbers of message with that id
            //update the total time
            iterator->second->total_time += ms;
        }
        //--------------------------------------------------------------------------------

        //-------------------- mod the status if needed -----------------------------------
        //check if the message is a start or a stop message
        if (check_start_message(msg)) {
            //change from IDLE to RUN if not already in RUN (if so, then do nothing)
            if (state == IDLE) {
                state = RUN;
                //create new start file
                counter_run_files++;
                runFile.open(file_name + "_run_" + to_string(counter_run_files) + ".log", ios::out);

                if(runFile.fail()){
                    //error opening the parse log file
                    cout << "Error opening the run file n." << counter_run_files << endl;
                    return 1;
                }

            }

        } else if (check_stop_message(msg)) {
            if(state == RUN) {
                //close run file
                runFile.close();
            }
            //change from START to IDLE
            state = IDLE;
            //end START status
        }
        //----------------------------------------------------------------------------

        //--------- writing data -----------------------------------------------------
        //if in start, saving values in map
        if (state == RUN) {
            //parse and save the message in the run file
            runFile << parseId(msg) << "#";

            //write every result from parsePayload giving a space for each byte
            payloadParse = parsePayload(msg);

            for(int i=0; i< strlen(payload)/2 && payloadParse != nullptr; i++){
                runFile << payloadParse[i] << " ";
            }

            runFile << endl;
            delete payloadParse;

            //finish if START
        } else if (state == IDLE){
            //parse and save the message in the idle file
            idleFile << parseId(msg) << "#";

            //write every result from parsePayload giving a space for each byte
            payloadParse = parsePayload(msg);

            for(int i=0; i<strlen(payload)/2 && payloadParse != nullptr; i++){
                idleFile << payloadParse[i] << " ";
            }

            idleFile << endl;
            delete payloadParse;
        }

        delete id;
        delete payload;
        //end while
    }

    //close files
    idleFile.close();
    runFile.close();
    readLog.close();

    //create statistics file
    createCSV(messages, file_name+"_stats.log");

    return 0;
}

char * createNameFile(){
    time_t rawtime;
    struct tm * timeinfo;
    char *buffer = new char[80];
    time (&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer,80,"%d-%m-%Y %H-%M-%S",timeinfo);
    return buffer;
}

bool check_start_message(string line){
    return (line == START_1 || line == START_2);
}

bool check_stop_message(string line){
    return (line == STOP_1);
}

int16_t parseId(const char * line){
    //split the message in two parts, id and payload
    char * id = getId(line);

    int size = strlen(id);

    if(size<=MAX_SIZE_ID){
        return hexadecimalToDecimal(id);
    }

    return -1;
}


short * parsePayload(const char * line){
    char * payload = getPayload(line);
    int size = strlen(payload);
    char buffer[3];//buffer to contain the two characters to parse
    short * results;//vector to contain the parsed pairs

    //check if we have an even pair of characters
    if(size<=MAX_SIZE_PAYLOAD && size%2==0){
        //everything ok
        results = new short[(size/2)];
        for(int i=0, j=0; i<size; i++, j++){
            buffer[0] = payload[i];
            buffer[1] = payload[i+1];
            buffer[2] = '\0';
            i++;
            results[j] = hexadecimalToDecimal(buffer);
        }

        return results;
    } else {
        //error in syntax
        return nullptr;
    }
}

int hexadecimalToDecimal(const char * hexVal){
    int len = strlen(hexVal);
    int base = 1;
    int dec_val = 0;

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

char * getId(const char * line){
    char * sub = new char[MAX_SIZE_ID];

    int i=0;
    for (; line[i] != '#'; i++){
        sub[i] = line[i];
    }
    sub[i] = '\0';

    return sub;
}

char * getPayload(const char *line){
    char *sub = new char[MAX_SIZE_PAYLOAD];
    int index = 0;

    //to exclude the id part
    for(char c; c!='#'; index++){
        c = line[index];
    }

    int size = strlen(line);
    int i=0;
    for(; index<size; i++, index++){
        sub[i] = line[index];
    }
    sub[i] = '\0';

    return sub;
}

int createCSV(auto &messages, const string& file_name){
    fstream csv;
    csv.open(file_name, ios::out);

    if(csv.fail()){
        cout << "Error creating csv file" << endl;
        return 1;
    }

    double mean;
    for (const auto &message: messages) {
        mean = message.second->total_time/message.second->nMsg;
        csv << message.first << "," << message.second->nMsg << "," << mean << "\n";
    }

    csv.close();
    messages.clear();//remove all data collected

    return 0;
}

char * getAbsolutePath(const char *file){
    auto path = std::filesystem::absolute(file);
    auto pathStr = path.string();
    string subString1 = "\\bin";//Windows system
    string subString2 = "/bin";//linux system

    //remove the bin directory to find files in the project directory
    int start_position_to_erase = pathStr.find(subString1);
    int start_position_to_erase2 = pathStr.find(subString2);

    //in case we are in Windows system
    if(start_position_to_erase != string::npos)
        pathStr.erase(start_position_to_erase,subString1.length());

    //in case we are in linux system
    if(start_position_to_erase2 != string::npos)
        pathStr.erase(start_position_to_erase2,subString2.length());

    char *p = new char[MAX_PATH_SIZE];
    strcpy(p, pathStr.c_str());

    return p;
}