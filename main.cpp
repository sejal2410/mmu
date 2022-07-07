#include <iostream>
#include <fstream>
#include <string>
#include <getopt.h>
#include <cstring>
#include <list>
#include<vector>
using namespace std;

const int MAX_VPAGES = 64;
int MAX_FRAMES = 128;
int rlinenum;
FILE* inpfile;
FILE* rfile;
static char linebuf[1024];
const char* DELIM = " \t\n\r\v\f";
unsigned long long context_switches = 0, exits = 0, cost = 0, instr_counter=0;
int randNumCount=0, ofs=0;
vector<int> randomVals;
struct VMA{
    int start_vpage;
    int end_vpage;
    int write_protected;
    int file_mapped;
};

struct frame_t_entry{
    int fid;
    int curr_pid = -1;
    int curr_vpage;
    unsigned int age:32;
    int last_use = 0;

};

struct pte_t{
    unsigned int present:1;
    unsigned int referenced:1;
    unsigned int pagedout:1;
    unsigned int modified:1;
    unsigned int write_protected:1;
    unsigned int valid_VMP:1;
    unsigned int file_mapped:1;
    unsigned int frame_num:7;
    unsigned int padding : 18;
};

struct Process{
    int pid;
    vector<VMA*> vma_list;
    pte_t page_table[MAX_VPAGES];
    long unsigned int UNMAP = 0;
    long unsigned int MAP = 0;
    long unsigned int IN = 0;
    long unsigned int FIN = 0;
    long unsigned int OUT = 0;
    long unsigned int FOUT = 0;
    long unsigned int SEGV = 0;
    long unsigned int SEGPROT = 0;
    long unsigned int ZERO = 0;
};
frame_t_entry frame_table[128];
vector<Process*> pro_vector;
Process* CURRENT_PROCESS = NULL;


class Pager {
public:
    virtual frame_t_entry* select_victim_frame() = 0;
};

class FIFO : public Pager{

public:
    int hand;
    FIFO(){
       hand = 0;
       //cout<<"FIFO\n";
    }
    frame_t_entry* select_victim_frame(){
        frame_t_entry* victim_frame = &frame_table[hand];
        hand = hand + 1;
        if(hand >= MAX_FRAMES){
            hand =  0;
        }
        return victim_frame;
    }
};
int myrandom(int max_frames);
class Random : public Pager{
public:
    Random(){
      // cout<<"Random\n";
    }
    frame_t_entry* select_victim_frame(){
        frame_t_entry* victim_frame = &frame_table[myrandom(MAX_FRAMES)];
        return victim_frame;
    }
};

class Clock : public FIFO{
public:
    Clock(){
     //   cout<<"Clock\n";
    }
    frame_t_entry* select_victim_frame(){
        frame_t_entry* fte = &frame_table[hand];
        while(pro_vector[fte->curr_pid]->page_table[fte->curr_vpage].referenced==1) {
         //   cout<< "Debug Inside: "<<pro_vector[fte->curr_pid]->page_table[fte->curr_vpage].referenced<<endl;
            pte_t* pte =  &pro_vector[fte->curr_pid]->page_table[fte->curr_vpage];
            pte->referenced=0;
            hand++;
            if(hand >= MAX_FRAMES){
                hand = 0;
            }
            fte = &frame_table[hand];
        }
        hand++;
        if(hand >= MAX_FRAMES){
            hand = 0;
        }
       // cout<< "Debug: "<<pro_vector[fte->curr_pid]->page_table[fte->curr_vpage].referenced<<endl;
        return fte;
    }
};

class NRU : public Pager{
    int class_idx;
    frame_t_entry* fte;
    unsigned long long next_reset;
    bool reset_reference_bits;
    int hand;
public:
    NRU(){
        hand=0;
        class_idx=5;
        next_reset=50;
        reset_reference_bits=false;
      //  cout<<"NRU\n";
    }
    frame_t_entry* select_victim_frame(){
        fte=NULL;
        class_idx=5;
        reset_reference_bits= false;
        if (instr_counter >= next_reset)
        {
            this->next_reset = instr_counter + 50;
            reset_reference_bits= true;
        }
            int temp = hand-1;
            if(temp<0)
                temp=MAX_FRAMES-1;
            while(hand!=temp){
                frame_t_entry * frame = &frame_table[hand];
                pte_t* pte = &pro_vector[frame->curr_pid]->page_table[frame->curr_vpage];
                int class_index = 2* pte->referenced+ pte->modified;
                if(reset_reference_bits)
                    pte->referenced=0;
                if(class_index<class_idx){
                    class_idx = class_index;
                    fte = frame;
                }
                hand++;
                if(hand >= MAX_FRAMES)
                    hand = 0;
              //  cout<<"Debug: "<<frame->curr_pid<<" "<<frame->curr_vpage<<" Class Index: "<<class_index<<endl;
               // cout<<"Debug1: "<<fte->curr_pid<<" "<<fte->curr_vpage<<" ClassIndex: "<<class_idx<<endl;
            }
            if(hand==temp){
                frame_t_entry * frame = &frame_table[hand];
                pte_t* pte = &pro_vector[frame->curr_pid]->page_table[frame->curr_vpage];
                int class_index = 2* pte->referenced+ pte->modified;
                if(reset_reference_bits)
                    pte->referenced=0;
                if(class_index<class_idx){
                    class_idx = class_index;
                    fte = frame;
                }
            }
        hand = fte->fid + 1;
         //   cout<<"Next position: "<<hand<<" Referencebit boolean "<<reset_reference_bits<<endl;
        if(hand >= MAX_FRAMES) hand = 0;
        return fte;
    }
};

class Aging : public Pager{
    int hand;
public:
    Aging(){
        hand=0;
    }
    frame_t_entry * select_victim_frame() {
        frame_t_entry* fte = NULL;
        int temp = hand;
        do{
            frame_t_entry* frame = &frame_table[hand];
            pte_t* pte = &pro_vector[frame->curr_pid]->page_table[frame->curr_vpage];
            frame->age = frame->age >> 1;
            if(pte->referenced){
                pte->referenced = 0;
                frame->age = frame->age | 0x80000000;
            }
            if(fte == NULL || frame->age < fte->age){
                fte = frame;
            }
            hand++;
            if(hand >= MAX_FRAMES)
                hand = 0;
        }
        while(hand != temp);
        hand = fte->fid + 1;
        if(hand >= MAX_FRAMES)
            hand = 0;
        return fte;
    }
};

class WorkingSet : public Pager{
    int hand;

public:
    WorkingSet(){
        hand=0;
   //     victim=NULL;
    }
    frame_t_entry * select_victim_frame() {
        frame_t_entry* victim = NULL;
        int temp = hand;
        do{
            frame_t_entry* fte = &frame_table[hand];
            pte_t* pte = &pro_vector[fte->curr_pid]->page_table[fte->curr_vpage];
            if(pte->referenced==1){
                pte->referenced=0;
                fte->last_use=instr_counter;
                if(victim == NULL){
                    victim = fte;
                }
            }
            else{
                int age = instr_counter - fte->last_use;
                if(age>49) {
                    victim = fte;
                    break;
                }
                else{
                    if(victim==NULL)
                        victim=fte;
                    else if(fte->last_use < victim->last_use)
                        victim=fte;
                }
            }
            hand = hand + 1;
            if(hand>= MAX_FRAMES)
                hand = 0;
        }while(hand!=temp);

        hand = victim->fid + 1;
        if(hand>= MAX_FRAMES)
            hand = 0;
        return victim;
    }


};

Pager* PAGER;
deque<frame_t_entry*> free_frames;
frame_t_entry* allocate_frame_from_free_list(){
    if(!free_frames.empty()){
        frame_t_entry* frame = free_frames.front();
        free_frames.pop_front();
        return frame;
    }
    return NULL;
}

frame_t_entry* get_frame() {
    frame_t_entry *frame = allocate_frame_from_free_list();
    //-> figure out if/what to do with old frame if it was mapped
    // see general outline in MM-slides under Lab3 header and writeup below
    // see whether and how to bring in the content of the access page.
    if (frame != NULL)
        return frame;
    frame = PAGER->select_victim_frame();
    pte_t* page_table_entry = &pro_vector[frame->curr_pid]->page_table[frame->curr_vpage];
    cout<<" UNMAP "<<frame->curr_pid<<":"<<frame->curr_vpage<<endl;
    cost+=400;
    pro_vector[frame->curr_pid]->UNMAP+=1;

    if(page_table_entry->modified){
        if(page_table_entry->file_mapped) {
            cout << " FOUT\n";
            pro_vector[frame->curr_pid]->FOUT+=1;
            cost+=2400;
        }
        else {
            page_table_entry->pagedout=1;
            cout << " OUT\n";
            pro_vector[frame->curr_pid]->OUT+=1;
            cost+=2700;
        }
        //reset entry of the page table
        page_table_entry->modified=0;
    }
    page_table_entry->present=0;
    page_table_entry->referenced=0;
    page_table_entry->frame_num=0;
    return frame;
}

bool get_next_instruction(char* operation, int* vpage){
    while(fgets(linebuf,1024, inpfile)!=NULL){
        if(linebuf[0]!='#'){
            break;
        }
    }
    if(linebuf!=NULL) {
        if(linebuf[0]=='#')
            return false;
        char *co = strtok(linebuf, DELIM);
        *operation = co[0];
        char op=*operation;
        char *vp = strtok(NULL, DELIM);
        *vpage = atoi(vp);
        int v = *vpage;
        printf("%d: ==> %c %d\n",instr_counter ,op ,v);
        instr_counter++;
        return true;
    }
    else
        return false;

}
Process* runProcess(int processId){
    for(Process* p: pro_vector)
        if(p->pid==processId)
            return  p;
}

VMA* valid_vma(int vpage){
    vector<VMA*> vma_list = CURRENT_PROCESS->vma_list;
    for(VMA* vma : vma_list){
        if(vpage>=vma->start_vpage && vpage<=vma->end_vpage) {
         //   cout << vma->start_vpage << "    " << vma->end_vpage << " : ";
            return vma;
        }
    }
    return NULL;
}

void free_allocated_frames(){
    for(int i=0;i<MAX_VPAGES;i++){
        pte_t* page_table_entry = &CURRENT_PROCESS->page_table[i];
        frame_t_entry* frame = &frame_table[page_table_entry->frame_num];
            //Unmap and fount and out modification
            if(page_table_entry->present==1){
                cout<<" UNMAP "<<CURRENT_PROCESS->pid<<":"<<frame->curr_vpage<<endl;
                CURRENT_PROCESS->UNMAP+=1;
                cost+=400;
                if(page_table_entry->modified && page_table_entry->file_mapped==1){
                    CURRENT_PROCESS->FOUT+=1;
                    cout<<" FOUT\n";
                    cost+=2400;
                }
                frame->curr_pid=-1;
                frame->age=0;
                frame->last_use=0;
                frame->curr_vpage=MAX_VPAGES+1;
                free_frames.push_back(frame);
            }
            page_table_entry->file_mapped=0;
            page_table_entry->pagedout=0;
            page_table_entry->present=0;
            page_table_entry->modified=0;
            page_table_entry->referenced=0;
            page_table_entry->frame_num=MAX_FRAMES+1;
            page_table_entry->write_protected=0;
    }
}

  void simulation() {
    char operation;
    int vpage;
    while (!get_next_instruction(&operation, &vpage)==NULL) {
// handle special case of “c” and “e” instruction
        if (operation == 'c') {
            CURRENT_PROCESS = runProcess(vpage);
            context_switches+=1;
            cost+=130;
       }
        else if(operation=='e') {//exit
            exits+=1;
            cost+=1250;
            cout << "EXIT current process " <<CURRENT_PROCESS->pid <<endl;
            free_allocated_frames();
        }
// now the real instructions for read and write
        else { // read and write instruction
            pte_t *pte = &CURRENT_PROCESS->page_table[vpage];
            if (pte->present==0) {
// this in reality generates the page fault exception and now you execute
// verify this is actually a valid page in a vma if not raise error and next inst
                VMA* vma = valid_vma(vpage);
                if(vma==NULL) {
                    CURRENT_PROCESS->SEGV+=1;
                    cost+=340;
                    cout<<" SEGV\n";
                }
                else{
                    pte->present = 1;
                    cost+=300;
                    pte->file_mapped = vma->file_mapped;
                    pte->write_protected = vma->write_protected;
                    frame_t_entry *newframe = get_frame();
                    //CURRENT_PROCESS->page_table->frame_num = newframe->fid;
                    CURRENT_PROCESS->MAP+=1;
                    if (pte->file_mapped) {
                            cout << " FIN" << endl;
                            CURRENT_PROCESS->FIN+=1;
                            cost+=2800;
                        }
                    else if(pte->pagedout) {
                            cout << " IN" << endl;
                            CURRENT_PROCESS->IN+=1;
                            cost+=3100;
                        }
                    else {//never swapped out
                        cout << " ZERO\n";
                        CURRENT_PROCESS->ZERO+=1;
                        cost+=140;
                    }
                    //reverse mapping
                    newframe->curr_pid = CURRENT_PROCESS->pid;
                    newframe->curr_vpage = vpage;
                    newframe->age = 0;
                    pte->frame_num = newframe->fid;
                    printf(" MAP %d\n",newframe->fid);
                }
            }
            pte->referenced=1;
            // check write protection
// simulate instruction execution by hardware by updating the R/M PTE bits

            if(!pte->write_protected && operation=='w')
                pte->modified=1;
            if(pte->write_protected && operation=='w'){
                CURRENT_PROCESS->SEGPROT+=1;
                cost+=420;
                cout<<" SEGPROT\n";
            }
            //read and write cost
            cost+=1;
        }
    }
}
void readInputFile() {
            while (fgets(linebuf, 1024, inpfile)) {
                if (linebuf[0] != '#')
                    break;
            }
            int num_process = atoi(strtok(linebuf, DELIM));
            //cout<<"Num processes: "<< num_process<<endl;
            int i = 0;
            Process *p;
            while (num_process>0) {
                num_process--;
                while (fgets(linebuf, 1024, inpfile) != NULL){
                    if (linebuf[0] != '#')
                        break;
                }
                //printf("Linbuffer is: %s", linebuf);
                p = new Process();
                p->pid = i++;
                int vma_num = atoi(strtok(linebuf, DELIM));
                //printf("VMA nums: %d\n",vma_num);
                VMA *vma_entry;
                while (vma_num>0) {
                   // printf("VMAs iterated are : %d %d\n",vma_entry->start_vpage, vma_entry->end_vpage);
                    vma_num--;
                    vma_entry = new VMA();
                    fgets(linebuf, 1024, inpfile);
                    if (linebuf[0] == '#')
                        continue;
                    vma_entry->start_vpage = atoi(strtok(linebuf, DELIM));
                    vma_entry->end_vpage = atoi(strtok(NULL, DELIM));
                    vma_entry->write_protected =atoi(strtok(NULL, DELIM));
                    vma_entry->file_mapped = atoi(strtok(NULL, DELIM));
                    p->vma_list.push_back(vma_entry);
                }
                pro_vector.push_back(p);
            }
}

void intialize_policy(char policy){
    if(policy=='f')
        PAGER = new FIFO();
    else if(policy=='r')
        PAGER= new Random();
    else if(policy=='c')
        PAGER = new Clock();
    else if(policy=='e')
        PAGER = new NRU();
    else if(policy=='w')
        PAGER = new WorkingSet();
    else if(policy=='a')
        PAGER = new Aging();
}

// Figure this one out later
int parseInput(int argc, char *argv[]) {
    int argument;
    string OPFS;
    char policy;
    int args=0;
    while ((argument = getopt(argc, argv, "o:a:f:")) != -1) {
        args++;
        switch(argument){

            case 'o':
                OPFS = std::string(optarg);
                break;
            case 'f':
                MAX_FRAMES = atoi(optarg);
                break;
            case 'a':
                policy = optarg[0];
                //cout<<"Policy selected: "<<policy<<endl;
                intialize_policy(policy);
                break;

        }

    }
    return args;
}

void readRandomFile(){
    char line[1024];
    fgets(line,1024, rfile);
    char* tok = strtok(line, DELIM);
    randNumCount = atoi(tok);
    while(fgets(line,1024, rfile)){
        char* tok = strtok(line, DELIM);
        randomVals.push_back(atoi(tok));
    }
}

int myrandom(int max_frames) {
    if(ofs==randNumCount)
        ofs=0;
    return (randomVals[ofs++] % max_frames);
}

int main(int argc, char *argv[]){
        int args = parseInput(argc, argv);
        char* inputFileName = argv[4];
        inpfile = fopen(inputFileName, "r");
        char* randomFile = argv[5];
        rfile = fopen(randomFile, "r");
        readInputFile();
        readRandomFile();
        for(int i=0; i<MAX_FRAMES; i++) {
            frame_t_entry* frame;
            frame_table[i].fid = i;
            frame = &frame_table[i];
            free_frames.push_back(frame);
        }
        simulation();
//    process table
      int proc_index=0;
      for(Process* proc : pro_vector){
          int pte=0;
          printf("PT[%d]: ", proc_index++);
           for(pte_t page_table_entry: proc->page_table){
               if(page_table_entry.present){
                   printf("%d:",pte);
                   cout << (page_table_entry.referenced==1 ? "R" : "-");
                   cout << (page_table_entry.modified==1 ? "M" : "-");
                   cout << (page_table_entry.pagedout==1 ? "S" : "-");
               }
               else{
                   //cout << (page_table_entry.pagedout==1 ? "#" : "*");
                   if(page_table_entry.pagedout){
                       cout << "#";
                   }
                   else{
                       cout << "*";
                   }
               }
               pte++;
               if(pte!=MAX_VPAGES)
                    cout << " ";

           }
           cout<<endl;
      }

//    frame table
    cout<<"FT: ";
    int max = 0;
    for (frame_t_entry frame : frame_table)
    {   if(max==MAX_FRAMES)
            break;
        max++;
        if (frame.curr_pid == -1)
            if(max!=MAX_FRAMES)
                cout<<"* ";
            else
                cout<<"*";
        else if(max!=MAX_FRAMES)
            printf("%d:%d ", frame.curr_pid, frame.curr_vpage);
        else
            printf("%d:%d", frame.curr_pid, frame.curr_vpage);
    }
    cout<<endl;
//    process stats
    for(Process* proc : pro_vector){
        printf("PROC[%d]: U=%lu M=%lu I=%lu O=%lu FI=%lu FO=%lu Z=%lu SV=%lu SP=%lu\n",
               proc->pid,
               proc->UNMAP, proc->MAP, proc->IN, proc->OUT,
               proc->FIN, proc->FOUT, proc->ZERO,
               proc->SEGV, proc->SEGPROT);
    }
    pte_t pte1;
    printf("TOTALCOST %lu %lu %lu %llu %lu\n",
           instr_counter, context_switches, exits, cost, sizeof(pte1));


}