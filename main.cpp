#include <string>
#include <iostream>
#include <fstream>
#include <vector>
#include <bitset>
#include <cstring>
#include <utility>
#include <map>
#define DATABLOCK_DATA_SIZE 1024
#define FILENAME_SIZE 52
#define FILES_NUM 64

using namespace std;

struct Header
{
    unsigned int drive_size = 0;
    unsigned int indexnodes_section_begin = 0;
    unsigned int datablocks_section_begin = 0;
};

struct IndexNode
{
    unsigned int datablocks_begin = 0;
    unsigned int file_size_bytes = 0;
    char filename[FILENAME_SIZE] = {0};
};

struct DataBlock
{
    unsigned int next_address = 0;
    char data[DATABLOCK_DATA_SIZE] = {0};
};

class Drive
{
    public:

    void allocate_space(int size_MB)
    {
        ofstream drive(drive_name, ios::binary | ios::out);
        vector<char> empty(1024, 0);
        for(int i = 0;i < 1024 * size_MB;i++)
        {
            if (!drive.write(&empty[0], empty.size()))
            {
                cerr << "Error: Problem with writing empty drive.\n" << endl;
                throw;
            }
        }
        drive.close();
    }

    void fill_table_sizes(unsigned int size_MB)
    {
        unsigned int size_B = size_MB * 1048576;
        indexnodes_bitmap_size_bytes = (FILES_NUM + 7) / 8;
        indexnodes_size_bytes = FILES_NUM * sizeof(IndexNode);
        datablocks_num = (8 * (size_B - (sizeof(Header) + indexnodes_bitmap_size_bytes + indexnodes_size_bytes)) - 7) / (sizeof(DataBlock) * 8 + 1);
        datablocks_bitmap_size_bytes = (datablocks_num + 7) / 8;
    }

    void read_header()
    {
        ifstream drive(drive_name, ios::binary | ios::in);
        if(!drive){
            cerr << "Error: No drive with such name\n";
            throw;
        }
        drive.read((char*)&header_info, sizeof(Header));
    }

    void fill_header(unsigned int size_MB)
    {
        fill_table_sizes(size_MB);
        unsigned int indexnodes_begin = sizeof(Header) + indexnodes_bitmap_size_bytes + datablocks_bitmap_size_bytes;
        unsigned int datablocks_begin = indexnodes_begin + indexnodes_size_bytes;
        header_info = {size_MB, indexnodes_begin, datablocks_begin};
    }

    void mkdrive(const string& drive_name, unsigned int size_MB)
    {
        this->drive_name = drive_name;
        allocate_space(size_MB);
        fill_header(size_MB);
        save_header();
    }

    void rmdrive(const string& drive_name)
    {
        remove(drive_name.c_str());
    }

    void load_drive(const string& drive_name)
    {
        this->drive_name = drive_name;
        read_header();
        fill_table_sizes(header_info.drive_size);
        print_drive();
        if ((sizeof(Header) + indexnodes_bitmap_size_bytes + datablocks_bitmap_size_bytes) != header_info.indexnodes_section_begin)
            throw "invalid INodes Section Start Address";
        if ((sizeof(Header) + indexnodes_bitmap_size_bytes + datablocks_bitmap_size_bytes + indexnodes_size_bytes) != header_info.datablocks_section_begin)
            throw "invalid DataBlocks Section Start Address";
        load_bitmap_indexnodes();
        load_bitmap_datablocks();
    }

    void print_bitmap()
    {
        cout << "IndexNodes bitmap" << endl;
        for (const auto& bul : indexnodes_bitmap) cout << bul << " ";
        cout << endl;
        cout << "DataBlocks bitmap" << endl;
        for (const auto& bul : datablocks_bitmap) cout << bul << " ";
        cout << endl;
    }

    void add_file(const string& filename)
    {
        // check filename
        if (filename.size() > FILENAME_SIZE)
        {
            cout << "File name too long" << endl;
            return;
        }
        // check file
        ifstream file(filename, ios::binary | ios::in);
        if (!file)
        {
            cerr << "Unable to get file";
            return;
        }
        // calculate filesize
        streampos file_pos = 0;
        file_pos = file.tellg();
        file.seekg(0, ios::end);
        file_pos = file.tellg() - file_pos;
        file.close();
        // check for space on drive 
        unsigned int size = file_pos;
        unsigned long space_needed = (size + DATABLOCK_DATA_SIZE - 1)/DATABLOCK_DATA_SIZE;
        const auto space_indexnodes = fetch_free_indexnodes_num();
        const auto space_datablocks = fetch_free_datablocks_num();
        if (!(space_indexnodes > 0) || !(space_datablocks >= space_needed))
        {
            cerr << "Disheartening lack of space, please indulge in buying a new drive\n";
            return;
        }
        unsigned long free_index = fetch_free_indexnode();
        vector<unsigned long> free_datablocks = fetch_free_datablocks(space_needed);
        for(auto x: free_datablocks) cout << x << " ";
        cout << endl;
        indexnodes_bitmap[free_index] = true;
        // fill datablocks with data from file
        vector<DataBlock> datablock_vector;
        file.open(filename, std::ios::binary | std::ios::in);
        if (!file)
        {
            cerr << "Unable to get file";
            return;
        }
        for (unsigned long i = 0; i < free_datablocks.size(); i++)
        {
            char buffer[DATABLOCK_DATA_SIZE] = {0};
            datablocks_bitmap[free_datablocks[i]] = true;
            file.read((char*)buffer, DATABLOCK_DATA_SIZE);
            unsigned int next_address = 0;
            if (i < free_datablocks.size() - 1) next_address = free_datablocks[i+1] * sizeof(DataBlock) + header_info.datablocks_section_begin;
            DataBlock datablock;
            datablock.next_address = next_address;
            memcpy((char*)datablock.data, (char*)buffer, DATABLOCK_DATA_SIZE);
            datablock_vector.push_back(datablock);
        }
        unsigned int free_datablock_address = free_datablocks[0] * sizeof(DataBlock) + header_info.datablocks_section_begin;
        save_datablocks(datablock_vector, free_datablock_address);
        IndexNode indexnode = {free_datablock_address, size, ""};  
        strncpy(indexnode.filename, filename.c_str(), filename.size());
        unsigned int _INodeAddr = free_index * sizeof(IndexNode) + header_info.indexnodes_section_begin;
        save_indexnodes(indexnode, _INodeAddr);
        save_bitmap_indexnodes();
        save_datablocks_bitmap();
        cout << "ADD WENT WELL (HOPEFULLY)" << endl;
        return;
    }

    void ls()
    {
        for(unsigned long i = 0;i < indexnodes_bitmap.size();i++)
        {
            if (!indexnodes_bitmap[i]) continue;
            IndexNode indexnode = load_indexnodes(i*sizeof(IndexNode) + header_info.indexnodes_section_begin);
            cout << "Index " << i << ": " << indexnode.filename << "  \t|||  " << indexnode.file_size_bytes << "[B]  \t|||   " << indexnode.datablocks_begin << endl;
        }
    }

    void rm(unsigned long file_index){
        if (!indexnodes_bitmap[file_index]){
            cerr<<"Error: File with this Index does not exist on drive.\n";
            return;
        }
        unsigned int indexnode_address = file_index * sizeof(IndexNode) + header_info.indexnodes_section_begin;
        IndexNode indexnode = load_indexnodes(indexnode_address);
        vector<DataBlock> datablock_vector = load_datablocks(indexnode.datablocks_begin);
        
        unsigned int next_address = indexnode.datablocks_begin; 
        for (const auto& db : datablock_vector){
            unsigned long DataBlockIndex = (next_address - header_info.datablocks_section_begin) / sizeof(DataBlock);
            next_address = db.next_address; 
            //could clear DataBlock
            datablocks_bitmap[DataBlockIndex] = 0;
        }
        unsigned long INodeIndex = (indexnode_address - header_info.indexnodes_section_begin) / sizeof(IndexNode);
        indexnodes_bitmap[INodeIndex] = 0;
        save_bitmap_indexnodes();
        save_datablocks_bitmap();
        return;
    }

    void get_file(unsigned long file_index, const string& to_save_filename){
        if (!indexnodes_bitmap[file_index])
        {
            cerr << "Incorrect index\n";
            return;
        }
        unsigned int indexnode_address = file_index * sizeof(IndexNode) + header_info.indexnodes_section_begin;
        IndexNode indexnode = load_indexnodes(indexnode_address);
        vector<DataBlock> datablock_vector = load_datablocks(indexnode.datablocks_begin);
        ofstream file;
        file.open(to_save_filename, ios::binary | ios::out);
        unsigned int bytes_left = indexnode.file_size_bytes;
        for (const auto & db : datablock_vector)
        {
            if (bytes_left < 0)
            {
                cerr << "corrupted drive\n";
                throw "corrupted drive";
            }
            if (bytes_left >= DATABLOCK_DATA_SIZE){
                file.write((char*)db.data, DATABLOCK_DATA_SIZE);
                bytes_left-=DATABLOCK_DATA_SIZE;
            }
            else{
                file.write((char*)db.data, bytes_left);
                bytes_left-=bytes_left; // bytes_left = 0;
            }
        }
        file.close();
    }

    void print_drive()
    {
        cout << "DISK INFO \n" << drive_name << "  |||  " << header_info.drive_size << "MB  |||  " << header_info.indexnodes_section_begin << "  |||  " << header_info.datablocks_section_begin << endl;
    }

    vector<char> bits_to_bytes(const vector<bool>& vector_bits) const
    {
        unsigned long bytes_count = (vector_bits.size() + 7) / 8;
        vector<char> bytes_vector;
        bytes_vector.resize(bytes_count, 0);
        for (unsigned long i = 0; i < vector_bits.size(); ++i)
        {
            unsigned long byteIndex = i / 8; 
            unsigned long bitPosition = i % 8;
            if (vector_bits[i]) bytes_vector[byteIndex] |= (1 << bitPosition);
        }
        return bytes_vector;
    }

    vector<bool> bytes_to_bits(const vector<char>& bytes_vector, unsigned long bits) const
    {
        vector<bool> vector_bits;
        int bitsCount = 0;
        for (char byte : bytes_vector)
        {
            for (int i = 0; i < 8; ++i)
            {
                if (bitsCount++ > bits) return vector_bits;
                bool bit = (byte >> i) & 1;
                vector_bits.push_back(bit);
            }
        }
        return vector_bits;
    }

    void save_datablocks(const vector<DataBlock>& datablock_vector, unsigned int address)
    {
        ofstream drive(drive_name, ios::binary | ios::in | ios::out);
        if (!drive)
        {
            cerr << "Drive does not open\n";
            throw;
        }
        drive.seekp(address);
        drive.write((char*)&datablock_vector[0], sizeof(DataBlock));
        for (unsigned long i = 1; i < datablock_vector.size(); i++)
        {
            drive.seekp(datablock_vector[i-1].next_address);
            drive.write((char*)&datablock_vector[i], sizeof(DataBlock));
        }
        drive.close();
    }

    void save_bitmap_indexnodes()
    {
        unsigned int indexnodes_bitmap_address = sizeof(Header);
        save_vector_bits(indexnodes_bitmap, indexnodes_bitmap_address);
    }

    void save_datablocks_bitmap()
    {
        unsigned int datablocks_bitmap_address = sizeof(Header) + indexnodes_bitmap_size_bytes;
        save_vector_bits(datablocks_bitmap, datablocks_bitmap_address);
    }

    void save_vector_bits(const vector<bool>& vector_bits, unsigned int save_address) const
    {
        ofstream drive(drive_name, ios::binary | ios::in | ios::out);
        if (!drive)
        {
            cerr << "Drive does not open\n";
            throw;
        }
        //convert to bytes_vector
        vector<char> bytes_vector = bits_to_bytes(vector_bits);
        drive.seekp(save_address);
        drive.write((char*)bytes_vector.data(), bytes_vector.size());
        drive.close();
    }

    void save_header() const
    {
        ofstream drive(drive_name, ios::binary | ios::out | ios::in);
        drive.write((char*)&header_info, sizeof(Header));
        drive.close();
    }

    void save_indexnodes(const IndexNode& indexnode, unsigned int address)
    {
        ofstream drive(drive_name, ios::binary | ios::in | ios::out);
        if (!drive)
        {
            cerr << "Drive does not open\n";
            throw;
        }
        drive.seekp(address);
        drive.write((char*)&indexnode, sizeof(IndexNode));
        drive.close();
    }

    const unsigned int fetch_free_indexnodes_num() const
    {
        unsigned int free_indexnodes = 0;
        for (const auto& i: indexnodes_bitmap)
        {
            if(!i)
                free_indexnodes++;
        }
        return free_indexnodes;
    }
    
    const unsigned int fetch_free_datablocks_num() const{
        unsigned int freeDataBlocks = 0;
        for (const auto& n: datablocks_bitmap){
            if(!n)
                freeDataBlocks++;
        }
        return freeDataBlocks;
    }

    const unsigned long fetch_free_indexnode() const
    {
        for (unsigned long i = 0;i < indexnodes_bitmap.size(); i++)
        {
            if (!indexnodes_bitmap[i]) return i;
        }
        cerr << "IndexNode error\n";
        throw;
    }

    const vector<unsigned long> fetch_free_datablocks(unsigned long datablocks_num) const
    {
        vector<unsigned long> indexes;
        for (unsigned long i = 0;i < datablocks_bitmap.size();i++)
        {
            if (!datablocks_bitmap[i] && indexes.size() != datablocks_num) indexes.push_back(i);
        }
        if (indexes.size() < datablocks_num)
        {
            cerr << "Datablocks error\n";
            throw;
        }
        return indexes; 
    }

    vector<char> load_vector_bytes(unsigned long bytesNum, unsigned int load_address)
    {
        vector<char> bytes_vector(bytesNum, 0);
        ifstream drive(drive_name, ios::binary | ios::in);
        if (!drive)
        {
            cerr << "Drive does not open\n";
            throw;
        }
        drive.seekg(load_address);
        drive.read((char*)bytes_vector.data(), bytesNum);
        return bytes_vector;
    }

    void load_bitmap_indexnodes()
    {
        unsigned int indexnodes_bitmap_address = sizeof(Header);
        vector<char> indexnodes_vector_bytes = load_vector_bytes(indexnodes_bitmap_size_bytes, indexnodes_bitmap_address);
        indexnodes_bitmap = bytes_to_bits(indexnodes_vector_bytes, FILES_NUM);
    }

    void load_bitmap_datablocks()
    {
        unsigned int datablocks_bitmap_address = sizeof(Header) + indexnodes_bitmap_size_bytes;
        vector<char> datablocks_bytemap = load_vector_bytes(datablocks_bitmap_size_bytes, datablocks_bitmap_address);
        datablocks_bitmap = bytes_to_bits(datablocks_bytemap, datablocks_num);
    }

    IndexNode load_indexnodes(unsigned int address)
    {
        ifstream drive(drive_name, ios::binary | ios::in);
        if (!drive)
        {
            cerr << "Drive does not open\n";
            throw;
        }
        drive.seekg(address);
        IndexNode indexnode;
        drive.read((char*)&indexnode, sizeof(IndexNode));
        drive.close();
        return indexnode;
    }

    vector<DataBlock> load_datablocks(unsigned int address)
    {
        ifstream drive(drive_name, ios::binary | ios::in);
        if (!drive)
        {
            cerr << "Drive does not open\n";
            throw;
        }
        unsigned int next_address = address;
        vector<DataBlock> datablock_vector;
        while (next_address != 0)
        {
            drive.seekg(next_address);
            DataBlock datablock;
            drive.read((char*)&datablock, sizeof(DataBlock));
            next_address = datablock.next_address;
            datablock_vector.push_back(datablock);
        }
        return datablock_vector;
    }

    string drive_name;
    Header header_info;
    vector<bool> indexnodes_bitmap;
    vector<bool> datablocks_bitmap;
    unsigned int indexnodes_bitmap_size_bytes;
    unsigned int indexnodes_size_bytes;
    unsigned int datablocks_bitmap_size_bytes;
    unsigned int datablocks_num;
};

void printHelp()
{
    cout << "  rmdrive" << endl;
    cout << "  bitmap"  << endl;
    cout << "  ls" << endl;
    cout << "  add" << endl;
    cout << "  rm" << endl;
    cout << "  get" << endl;
    cout << "  h" << endl;
}

int main(int argc, char* argv[])
{
    Drive f;
    string choice;
    string name;
    string size;
    cout << "Enter drive or make a new one?" << endl;
    cin >> choice;
    if (choice == "make")
    {
        cout << "Enter drive name: ";
        cin >> name;
        cout << "\nEnter drive size in MB: ";
        cin >> size;
        try
        {
            f.mkdrive(name, stoi(size));
        }
        catch (...)
        {
            cerr << "<size> is not a positive integer\n";
            return 1;
        }
    }
    else if (choice == "enter")
    {
        cout << "Enter drive name: ";
        cin >> name;
    }
    else
    {
        cout << "ONLY 'enter' AND 'make' OPTIONS ARE AVAILABLE";
        return 1;
    }
    try
    {
        f.load_drive(name);
    }
    catch (...)
    {
        cout << "Error while entering the drive";
        return 1;
    }
    string command;
    string filename;
    string index;
    map<string, int> commands = {{"h", 0}, {"ls", 1}, {"bitmap", 2}, {"rmdrive", 3}, {"add", 4}, {"rm", 5}, {"get", 6}, {"exit", 7}, {"info", 8}};
    while(true)
    {
        cout << "$>";
        cin >> command;
        switch (commands[command])
        {
            case 0:
                printHelp();
                break;

            case 1:
                f.ls();
                break;

            case 2:
                f.print_bitmap();
                break;

            case 3:
                f.rmdrive(name);
                return 0;

            case 4:
                cout << "Enter file name: ";
                cin >> filename;
                f.add_file(filename);
                break;

            case 5:
                cout << "Enter file index: ";
                cin >> index;
                try
                {
                    f.rm(stoi(index));
                } 
                catch (...)
                {
                    cerr << "<index> is not a positive integer\n";
                    return 1;
                }
                break;

            case 6:
                cout << "Enter file index: ";
                cin >> index;
                cout << "Enter file name: ";
                cin >> filename;
                try {
                    filename = (filename != "") ? filename : (name + "_file" + index);
                    f.get_file(stoi(index), filename);
                }
                catch (...)
                {
                    cerr << "<index> is not a positive integer\n";
                    return 1;
                }
                break;
            
            case 7:
                return 0;

            case 8:
                f.print_drive();
                break;

            default:
                cerr << "Unknown command '" << command << "'\n";
                cerr << "Use 'h' for help\n";
                break;
        }
    }
    return 0;
}