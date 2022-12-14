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
const char *const START_1 = "0A0#6601";
const char *const START_2 = "0A0#FF01";
const char *const STOP_1 = "0A0#66FF";

//const for id and payload size
const int MAX_SIZE_ID = 3;
const int MAX_SIZE_PAYLOAD = 16;

//const for buffers size
const int MAX_PATH_SIZE = 500;
const int MAX_NAME_FILE = 100;

//counter of number of file
static int counter_files = 0;

//these are the possible states
enum STATUS{IDLE, RUN};

/**
 * find the absolute path of a file
 * @param file the file with a relative path
 * @return a string of the absolute path of the file
 */
char * getAbsolutePath(const char *file);

/**
 * create a unique string to use it as a name for a file using date and time system
 * @return the string created
 */
char * createNameFile();

/**
 * check if the message read is a start message
 * @param line the line read
 * @return true if is a start message, false otherwise
 */
bool check_start_message(const char * line);

/**
 * check if the message read is a stop message
 * @param line the line read
 * @return true if is a stop message, false otherwise
 */
bool check_stop_message(const char * line);

/**
 * parse the id from exa base to deca base
 * @param line the message read
 * @return the deca value of the message, -1 if some errors occur
 */
int16_t parseId(const char * line);

/**
 * parse the payload from exa base to deca base
 * @param line the message read
 * @return the deca values of the message of characters pairs, null if there's some errors
 */
short * parsePayload(const char * line);

/**
 * get the substring id from the message
 * @param line the message read
 * @return the id
 */
char * getId(const char * line);

/**
 * get the substring payload from the message
 * @param line the message read
 * @return the payload
 */
char * getPayload(const char * line);

/**
 * convert the exa string to decimal base value
 * @param hexVal the string to be parsed
 * @return the decimal value, -1 if is not possible to parse it
 */
int hexadecimalToDecimal(const char * hexVal);

/**
 * create a csv file using the values contained in the map
 * @param messages the map containing the values
 * @param file_name the name to give to the file
 * @return return -1 if some errors occurs
 */
int createCSV(auto &messages, const string& file_name);

/**
 * file run.txt saves every data that are in a run state
 *
 * file csv which saves the data from the run sessions doing some stats
 *
 * the file used for starting the CAN interface is candump.log
 */

int main() {
    //------------------ opening CAN interface --------------------------------------
    char * path = getAbsolutePath("candump.log");

    if(open_can(path) == -1){
        //error in opening CAN interface
        cout << "Error opening CAN interface";
        close_can();
        return 1;
    }

    delete path;
    //-------------------------------------------------------------------------------

    fstream runFile;//stream to write every run sessions

    //struct to contain values about a message
    typedef struct{
        int nMsg;//number of times of the msg
        double total_time;//total millis of each message
    } Values;

    //map containing stats about the id message
    map <string, Values *> messages;

    //the program start at IDLE state
    STATUS state = IDLE;

    //variables to get data from CAN interface
    string file_name = createNameFile();//name to give to the files
    char message[MAX_CAN_MESSAGE_SIZE];//the message read
    double ms;
    char *id;
    short parsedId;
    short * parsedPayload;
    chrono::time_point<std::chrono::system_clock> t1;//start time when reading messages in run mode

    //------------------ reading data from CAN interface --------------------------------------
    while(can_receive(message) != -1){//error or EOF

        //-------------------- modify the status if needed -----------------------------------
        //check if the message is a start or a stop message
        if (check_start_message(message)) {//start message
            //change from IDLE to RUN if not already in RUN (if so, then do nothing)
            if (state == IDLE) {
                state = RUN;
                counter_files++;

                //create new start file
                runFile.open(file_name + "-" + to_string(counter_files) + ".txt", ios::out);

                t1 = high_resolution_clock::now();//new start time of run mode

                if (runFile.fail()) {
                    //error opening the parse log file
                    cout << "Error opening the run file .txt n." << counter_files << endl;
                    runFile.close();
                    close_can();
                    return 1;
                }
                //end if state == IDLE
            }
            //end check_start_message()
        }

        if (check_stop_message(message)) {//stop message
            state = IDLE;
            runFile.close();
            //create statistics file for the ended run state
            createCSV(messages, file_name + "-" + to_string(counter_files) + ".csv");
        }

        //------------------------------------------------------------------------------------


        //--------- writing data ---------------------------------------------------------

        //---- parse message -------------------------------------------
        parsedId = parseId(message);
        parsedPayload = parsePayload(message);
        delete parsedPayload;
        //---------------------------------------------------------

        //writing on run file if in run state
        if(state == RUN) {
            //get data to write
            auto t2 = high_resolution_clock::now();//elapsed time from the start of run state
            duration<double, std::milli> ms_double = (t2 - t1);
            ms = ms_double.count();
            id = getId(message);

            //----- update map values ----------------------------------------------
            auto iterator = messages.find(id);
            //if there, increment counter and update mean
            //else, create new entry for key = ID
            if (iterator == messages.end()) {
                //create new struct for the current id
                auto *v = new Values;
                v->nMsg = 1;
                v->total_time = ms;
                messages.insert(pair<string, Values *>(id, v));
            } else {
                iterator->second->nMsg++;//update numbers of message with that id
                iterator->second->total_time += ms;//update the total time
            }
            //----------------------------------------------------------------------

            //write on file
            runFile << ms << " " << message << endl;

            delete id;
        }
        //end while
    }


    //-------- stop reading CAN interface -----------------------------------------
    //closing CAN interface and file
    close_can();
    runFile.close();

    //if there are some values in the map, write them
    //it means that no stop message was read but the data must be saved
    if(!messages.empty())
        createCSV(messages, file_name + "-" + to_string(counter_files) + ".csv");

    return 0;
}

char * createNameFile(){
    time_t rawtime;
    struct tm * timeinfo;
    char *buffer = new char[MAX_NAME_FILE];
    time (&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer,MAX_NAME_FILE,"%d-%m-%Y %H-%M-%S",timeinfo);
    return buffer;
}

bool check_start_message(const char *line){
    return (strcmp(line, START_1) == 0 || strcmp(line, START_2) == 0);
}

bool check_stop_message(const char *line){
    return (strcmp(line, STOP_1) == 0);
}

int16_t parseId(const char * line){
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
    short * results;//array pointer to contain the parsed pairs

    //check if we have an even pair of characters and the payload size is less or equal to the max size
    if(size<=MAX_SIZE_PAYLOAD && size%2==0){
        //everything ok
        results = new short[(size/2)];
        for(int i=0, j=0; i<size; i+=2, j++){
            //copy the 2 chars in the buffer to parse them
            buffer[0] = payload[i];
            buffer[1] = payload[i+1];
            buffer[2] = '\0';
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
        } else{
            //error, the string has some non hex values
            return -1;
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
    double mean;

    if(csv.fail()){
        cout << "Error creating csv file" << endl;
        return 1;
    }

    for (const auto &message : messages) {
        mean = message.second->total_time/message.second->nMsg;
        csv << message.first << "," << message.second->nMsg << "," << mean << endl;

        //remove struct pointer
        delete message.second;
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