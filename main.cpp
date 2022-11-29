#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <cstring>
#include <stdlib.h>
#include <unistd.h>
#include <ctype.h>
#include <deque>
#include <climits>

#define max_page_num 64
#define TAU 49
#define MAX_SIZE 30
using namespace std;

/*********************************** struct and class *************************/

typedef struct VMA{
public:
    int start_vpage;
    int end_vpage;
    bool write_protected;
    bool file_mapped;
} VAM;
typedef struct pte_t {
    unsigned int VALID : 1;
    unsigned int REFERENCED : 1;
    unsigned int MODIFIED : 1;
    unsigned int WRITE_PROTECT : 1;
    unsigned int PAGEDOUT : 1;
    unsigned int frame_number : 7;
    // 20 free bits
    unsigned int FILEMAPPED : 1;
    unsigned int CONFIGURATED : 1;

} pte_t;
typedef struct frame_t{
    int pid;
    int frame_id;
    int vpage;
    bool dirty;
    unsigned int counter : 32;
    unsigned long last_used_time;
} frame_t;
typedef struct summary_t{
    int sevg_count;
    int segprot_count;
    int unmap_count;
    int map_count;
    int pageins_count;
    int pageouts_count;
    int zero_count;
} summary_t;
class Process{
public:
    int pid;
    summary_t summary;
    vector<VMA> vma_vector;
    vector<pte_t> page_table;
    Process(): page_table(max_page_num){};

};

/****************************** global variable ******************************/

int frame_size;
int victim_frame_index = 0;
int instruction_count = 1;
int NRU_victim_index = 0;
int ofs = 0;
int* randvals;
int rand_num;
bool o_option = false;
bool page_table_option = false;
bool frame_table_option = false;
bool statistic_option = false;
string pager;
string options;
Process* curr_proc;
deque<frame_t> frame_table;
deque<frame_t*> victim_table;
deque<frame_t*> free_pool;
vector<Process*> proc_vector;
Process* proc;
vector<pair<string, int>> instruction_list;

/****************************** summary variable *****************************/

unsigned long long inst_count;
unsigned long long proc_exit_count;
unsigned long long context_switch_count;

/*********************************** Pagers ***********************************/
class Pager {
public:
    virtual frame_t* select_victim_frame() = 0;
    virtual void reset_counter(frame_t* victim_frame){};
};
class FIFO : public Pager{
public:
    frame_t* select_victim_frame(){
        if(victim_frame_index == frame_size){
            victim_frame_index = 0;
        }
        frame_t* frame = victim_table.at(victim_frame_index);
        victim_frame_index += 1;
        return frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class CLOCK : public Pager{
public:
    frame_t* select_victim_frame(){

        frame_t* victim_frame;

        while(true){
            if(victim_frame_index == frame_size){
                victim_frame_index = 0;
            }
            victim_frame = victim_table.at(victim_frame_index);
            Process* p = proc_vector[victim_frame->pid];
            pte_t* pte = &p->page_table[victim_frame->vpage];
            if(pte->REFERENCED){
                pte->REFERENCED = 0;
                victim_frame_index +=1;
                continue;
            }else{
                pte->REFERENCED = 1;
                victim_frame_index +=1;
                break;
            }
        }
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class NRU : public Pager{
public:
    frame_t* select_victim_frame(){

        if(victim_frame_index >= frame_size){
            victim_frame_index = 0;
        }

        int frame_count = 0;
        frame_t* victim_frame = victim_table[victim_frame_index];
        Process* p = proc_vector[victim_frame->pid];
        pte_t* victim_pte = &p->page_table[victim_frame->vpage];

        while(frame_count < frame_size){

            if(victim_frame_index >= frame_size){
                victim_frame_index = 0;
            }

            frame_t* curr_frame = victim_table[victim_frame_index];
            p = proc_vector[curr_frame->pid];
            pte_t* curr_pte = &p->page_table[curr_frame->vpage];
            //cout << "victim frame index: " + to_string(victim_frame_index) << endl;
            victim_frame_index += 1;
            frame_count += 1;

            int curr_pte_level = 2 * curr_pte->REFERENCED + curr_pte->MODIFIED;
            int victim_pte_level = 2 * victim_pte->REFERENCED + victim_pte->MODIFIED;

            if(curr_pte_level == 0){
                victim_frame = curr_frame;
                break;
            }
            else if(curr_pte_level < victim_pte_level){
                victim_pte = curr_pte;
                victim_frame = curr_frame;
            }
        }

        //reset REFERENCE bit
        if(instruction_count >= 50){
            cout << "instruction count " + to_string(instruction_count)<< endl;
            for(int i = 0; i < frame_table.size(); i++){
                Process* p = proc_vector[frame_table[i].pid];
                pte_t* pte = &p->page_table[frame_table[i].vpage];
                pte->REFERENCED = 0;
            }
            instruction_count = 1;
        }

        //increment victim_index by 1
        victim_frame_index = ((victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1);

        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class AGING: public Pager{
public:
    void reset_counter(frame_t* victim_frame){
        victim_frame->counter = 0;
    }
    frame_t* select_victim_frame(){

        /* 1) right shift counter by 1
         * 2) add current REFERENCE bit to most left
         * 3) reset REFERENCE bit
         * 4) min_counter = min(min_counter, curr_counter)
         */
        if(victim_frame_index >= frame_size){
            victim_frame_index = 0;
        }
        frame_t* victim_frame = &frame_table[victim_frame_index];
        Process* p = proc_vector[victim_frame->pid];
        pte_t* victim_pte = &p->page_table[victim_frame->vpage];
        frame_table[victim_frame_index].counter = frame_table[victim_frame_index].counter >> 1;
        unsigned int min_counter = frame_table[victim_frame_index].counter;
        if(victim_pte->REFERENCED == 1){
            min_counter = (min_counter | 0x80000000);
        }
        victim_frame_index += 1;

        for(int i = 0; i < frame_size; i++ ){

            if(victim_frame_index >= frame_size){
                victim_frame_index = 0;
            }
            frame_t* curr_frame = &frame_table[victim_frame_index];
            Process* curr_p = proc_vector[curr_frame->pid];
            pte_t* curr_pte = &curr_p->page_table[curr_frame->vpage];
            frame_table[victim_frame_index].counter = frame_table[victim_frame_index].counter >> 1;
            unsigned int curr_counter = frame_table[victim_frame_index].counter;
            if(curr_pte->REFERENCED == 1) {
                curr_counter = (curr_counter | 0x80000000);
            }
            curr_pte->REFERENCED = 0;

            //cout <<"curr: " + to_string(curr_counter) << endl;
            //cout <<"min: " + to_string(min_counter) << endl;
            if(min_counter > curr_counter){
                cout <<"vicim_frame_id " + to_string(curr_frame->frame_id) << endl;
                //cout <<"min: " + to_string(min_counter) << endl;
                victim_frame = curr_frame;
                min_counter = curr_counter;
            }

            victim_frame_index += 1;
        }

        victim_frame->counter = 0;
        victim_frame_index = (victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1;
        return victim_frame;
    }
};
class WS: public Pager{
public:
    frame_t* select_victim_frame(){

        unsigned long long min_time = ULLONG_MAX;
        frame_t* victim_frame = victim_table[victim_frame_index];

        for(int i = 0; i < frame_size; i++){

            if(victim_frame_index >= frame_size){
                victim_frame_index = 0;
            }

            frame_t* curr_frame = victim_table[victim_frame_index];
            Process* p = proc_vector[curr_frame->pid];
            pte_t* curr_pte = &p->page_table[curr_frame->vpage];

            unsigned long long frame_age = instruction_count - curr_frame->last_used_time;

            if(curr_pte->REFERENCED == 1){
                curr_pte->REFERENCED = 0;
                curr_frame->last_used_time = instruction_count;
            }else{
                if(frame_age > TAU) {
                    break;
                }
                else if(curr_frame->last_used_time < min_time){
                    victim_frame = curr_frame;
                    min_time = curr_frame->last_used_time;
                }
            }
            victim_frame_index += 1;
        }
        victim_frame_index = ((victim_frame->frame_id == frame_size) ? 0 : victim_frame->frame_id + 1);
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
class RANDOM: public Pager{
public:
    frame_t* select_victim_frame(){
        if(ofs == rand_num){
            ofs = 0;
        }
        int random_index = randvals[ofs] % frame_size;
        frame_t* victim_frame = &frame_table[random_index];
        ofs++;
        return victim_frame;
    }
    void reset_counter(frame_t* victim_frame){}
};
/*********************************** methods ***********************************/
bool not_sevg(Process* p, int vpage){
    int sevg_flag = false;
    for(int i = 0; i < p->vma_vector.size(); i++){
        if(vpage >= p->vma_vector[i].start_vpage && vpage <= p->vma_vector[i].end_vpage) {
            sevg_flag = true;
        }
    }
    return sevg_flag;
}
void readFile(char* inputPtr, char* rfilePtr){

    int total_proc = -1;
    int curr_proc_id = 0;
    int vma_num = 0;
    int vma_count = 0;
    bool is_new_proc = true;
    string line;
    string fileName(inputPtr);
    ifstream inputFile(fileName);

    if(!inputFile.is_open()){
        cout << "Cannot Open File" << endl;
        exit(1);
    }
    while(getline(inputFile, line)){

        if(line[0] == '#') continue;
        char* c = const_cast<char*>(line.c_str());
        char* token = strtok(c, " \t\n");

        // get number of processes
        if(total_proc == -1){
            total_proc = stoi(token);
            continue;
        }

        // read processes
        if(curr_proc_id < total_proc){
            vector<char*> temp;

            // 1) create new process 2) get total vma of new process
            if(is_new_proc){
                Process *p = new Process();
                p->pid = curr_proc_id;
                proc_vector.push_back(p);
                vma_num = stoi(token);
                vma_count = 0;
                is_new_proc = false;
                continue;
            }
            //read vma details
            else{
                while(token){
                    temp.push_back(token);
                    token = strtok(NULL, " \t\n");
                }
                VMA vma;
                vma.start_vpage = stoi(temp.at(0));
                vma.end_vpage = stoi(temp.at(1));
                vma.write_protected = stoi(temp.at(2));
                vma.file_mapped = stoi(temp.at(3));
                proc_vector.back()->vma_vector.push_back(vma);
                vma_count += 1;
                if(vma_num == vma_count){
                    is_new_proc = true;
                    curr_proc_id += 1;
                }
            }
        }
        //read instructions
        else{
            pair<string, int> p;
            p.first = string(token);
            token = strtok(NULL, " \t\n");
            p.second = stoi(token);
            //cout << "instruction " + p.first + " " + to_string(p.second) << endl;
            instruction_list.push_back(p);
        }
        inst_count = instruction_list.size();

        char buff[MAX_SIZE];
        string rFileName(rfilePtr);
        ifstream rFile(rFileName);
        rFile.getline(buff, MAX_SIZE); // get the first line, that's '40000'
        int num = atoi(buff); // num: 40000
        rand_num = num;
        randvals = new int[num];
        for(int i = 0; i < num; i++) {  // get the rest 40000 numbers
            rFile.getline(buff, MAX_SIZE);
            randvals[i] = atoi(buff);
        }
        rFile.close();
    }
    inputFile.close();
}
void initialize_frame_table(int frame_size){
    for(int i = 0; i < frame_size; i++){
        frame_t frame;
        frame.pid = -1;
        frame.dirty = false;
        frame.vpage = -1;
        frame.frame_id = i;
        frame.counter = 0;
        frame_table.push_back(frame);
    }
}
void initialize_free_pool(){
    for(int i = 0; i < frame_table.size(); i++){
        frame_t* frame = &frame_table.at(i);
        free_pool.push_back(frame);
    }
}
void configurate_pte(int pid, int vpage){
    Process* proc = proc_vector.at(pid);
    pte_t* pte = &proc->page_table.at(vpage);
    for(int i = 0; i < proc->vma_vector.size(); i++){
        VMA* vma = &proc->vma_vector.at(i);
        if(vpage >= vma->start_vpage && vpage <= vma->end_vpage){
            pte->FILEMAPPED = vma->file_mapped;
            pte->WRITE_PROTECT = vma->write_protected;
        }
    }
    pte->CONFIGURATED = 1;
}
void reset_frame_queue(){
    while(!victim_table.empty()){
        victim_table.pop_back();
    }
    initialize_free_pool();
}
Pager* set_pager(string pager){
    if(pager[0] == 'f') return new FIFO();
    if(pager[0] == 'r') return new RANDOM();
    if(pager[0] == 'c') return new CLOCK();
    if(pager[0] == 'e') return new NRU();
    if(pager[0] == 'a') return new AGING();
    if(pager[0] == 'w') return new WS();
    return nullptr;
}
void print_output(string options){
    cout << page_table_option << endl;
}
void simulation(){

    initialize_frame_table(frame_size);
    Pager* THE_PAGER = set_pager(pager);
    initialize_free_pool();

/*******************************************************************************/

    for(int i = 0; i < instruction_list.size(); i++) {

        string operation = instruction_list.at(i).first;
        int second = instruction_list.at(i).second;

        //context switch
        if (operation == "c") {
            context_switch_count += 1;
            curr_proc = proc_vector.at(second);
            printf("%d: ==> c %d\n", i, curr_proc->pid);
        }
        //read and write
        if (operation == "r" || operation == "w") {

            int vpage = second;
            printf("%d: ==> %s %d\n", i, operation.c_str(), vpage);
            pte_t *pte = &(curr_proc->page_table[vpage]);

            if (!(pte->CONFIGURATED)) {
                configurate_pte(curr_proc->pid, vpage);
            }

            if (!(pte->VALID)) {

                if (not_sevg(curr_proc, vpage) == false) {
                    curr_proc->summary.sevg_count +=1;
                    cout << " SEVG" << endl;
                    continue;
                }

                frame_t *victim_frame;
                //choose from free_pool
                if (free_pool.size() != 0) {
                    frame_t* free_frame = free_pool.front();
                    free_frame->pid = curr_proc->pid;
                    free_frame->vpage = vpage;
                    pte->frame_number = free_frame->frame_id;
                    victim_frame = free_frame;
                    victim_table.push_back(victim_frame);
                    free_pool.pop_front();
                }
                else { //choose victim frame
                    //cout << victim_frame_index << endl;
                    victim_frame = THE_PAGER->select_victim_frame();
                    cout << " UNMAP " << victim_frame->pid << ":" << victim_frame->vpage << endl;
                    // check modified and file mapped
                    int prev_proc_id = victim_frame->pid;
                    pte_t *prev_pte = &proc_vector.at(prev_proc_id)->page_table[victim_frame->vpage];
                    if (prev_pte->MODIFIED && prev_pte->FILEMAPPED) {
                        cout << " FOUT" << endl;
                    } else if (prev_pte->MODIFIED) {
                        prev_pte->PAGEDOUT = 1;
                        cout << " OUT" << endl;
                    }
                    //now victim_frame is available -> reset prev pte
                    prev_pte->VALID = 0;
                    prev_pte->MODIFIED = 0;
                    prev_pte->REFERENCED = 0;
                    prev_pte->frame_number = 0;
                }

                // reset curr pte and frame
                victim_frame->vpage = vpage;
                victim_frame->pid = curr_proc->pid;
                pte->frame_number = victim_frame->frame_id;
                pte->VALID = 1;

                if (pte->FILEMAPPED) {
                    cout << " FIN" << endl;
                } else if (pte->PAGEDOUT) {
                    cout << " IN" << endl;
                } else {
                    cout << " ZERO" << endl;
                }
                cout << " MAP " + to_string(victim_frame->frame_id) << endl;
            }

            pte->REFERENCED = 1;

            // write --> MODIFIED or SEGPROT
            if(pte->WRITE_PROTECT && operation == "w"){
                curr_proc->summary.segprot_count += 1;
                cout << " SEGPROT" << endl;
            }else if(operation == "w"){
                pte->MODIFIED = 1;
                //cout << "proc_id: " + to_string(curr_proc->pid) + " page M : " + to_string(curr_proc->page_table[vpage].MODIFIED) << endl;
            }
        }
        //exit
        if (operation == "e"){

            printf("%d: ==> e %d\n", i, curr_proc->pid);
            proc_exit_count += 1;
            printf("EXIT current process %d\n", curr_proc->pid);

            // check page table and reset flags
            for(int i = 0; i < curr_proc->page_table.size(); i++){
                summary_t summary = curr_proc->summary;
                pte_t* pte = &curr_proc->page_table[i];

                if(pte->VALID){
                    printf(" UNMAP %d:%d\n", curr_proc->pid, i );
                    summary.unmap_count += 1;
                    frame_t* frame = &frame_table[pte->frame_number];
                    frame->pid = -1;
                    frame->vpage = -1;
                    frame->counter = 0;
                    frame->last_used_time = 0;
                    frame->dirty = false;
                    free_pool.push_back(frame);
                }
                pte->VALID = 0;
                pte->frame_number = -1;
                pte->MODIFIED = 0;
                pte->WRITE_PROTECT = 0;
                pte->CONFIGURATED = 0;
                pte->PAGEDOUT = 0;
                pte->REFERENCED = 0;
                pte->FILEMAPPED = 0;
            }

            curr_proc = nullptr;
        }
        instruction_count += 1;
    }
}

int main(int argc, char *argv[]){

    int c;
    while ((c = getopt(argc, argv, "f:a:o:")) != -1){
        switch (c) {
            case 'f':
                frame_size = stoi(optarg);
                break;
            case 'a':
                pager = optarg;
                break;
            case 'o':
                options = optarg;
                for (auto &ch : options) {
                    switch (ch) {
                        case 'O':
                            o_option = true;
                            break;
                        case 'P':
                            page_table_option = true;
                            break;
                        case 'F':
                            frame_table_option = true;
                            break;
                        case 'S':
                            statistic_option = true;
                            break;
                        default:
                            break;
                    }
                }
            case '?':
                printf("error optopt: %c\n", optopt);
                printf("error opterr: %d\n", opterr);
                break;
            default:
                exit(EXIT_FAILURE);
                break;
        }
    }
//    cout << "frame_size: " + to_string(frame_size) << endl;
//    cout << "pager: " + pager << endl;
//    cout << "options: " + options << endl;
    readFile(argv[argc - 2], argv[argc - 1]);
    simulation();
    print_output(options);
}