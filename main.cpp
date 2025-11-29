#include <iostream>
#include <vector>
#include <string>
#include <optional>
#include <fstream>
#include <algorithm>
#include <ctime>
#include "json.hpp" 

using json = nlohmann::json;

struct Book {
    std::string Title;
    std::string Author;
    std::string ISBN;
    bool IsAvailable = true;

    void MarkAsLoaned() { IsAvailable = false; }
    void MarkAsAvailable() { IsAvailable = true; }

    json to_json() const {
        return json{{"Title", Title}, {"Author", Author}, {"ISBN", ISBN}, {"IsAvailable", IsAvailable}};
    }
    static Book from_json(const json &j) {
        Book b;
        b.Title = j.value("Title", "");
        b.Author = j.value("Author", "");
        b.ISBN = j.value("ISBN", "");
        b.IsAvailable = j.value("IsAvailable", true);
        return b;
    }
};

struct Reader {
    int Id = 0;
    std::string Name;
    std::string Email;

    json to_json() const {
        return json{{"Id", Id}, {"Name", Name}, {"Email", Email}};
    }
    static Reader from_json(const json &j) {
        Reader r;
        r.Id = j.value("Id", 0);
        r.Name = j.value("Name", "");
        r.Email = j.value("Email", "");
        return r;
    }
};

struct Loan {
    std::string BookISBN;
    int ReaderId = 0;
    std::string LoanDate; // ISO string
    std::optional<std::string> ReturnDate;

    json to_json() const {
        json j = {{"BookISBN", BookISBN}, {"ReaderId", ReaderId}, {"LoanDate", LoanDate}};
        if (ReturnDate) j["ReturnDate"] = *ReturnDate;
        else j["ReturnDate"] = nullptr;
        return j;
    }
    static Loan from_json(const json &j) {
        Loan l;
        l.BookISBN = j.value("BookISBN", "");
        l.ReaderId = j.value("ReaderId", 0);
        l.LoanDate = j.value("LoanDate", "");
        if (!j["ReturnDate"].is_null()) l.ReturnDate = j.value("ReturnDate", "");
        return l;
    }
};

static std::string now_iso() {
    std::time_t t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));
    return std::string(buf);
}

class LibraryManager {
public:
    std::vector<Book> Books;
    std::vector<Reader> Readers;
    std::vector<Loan> Loans;

    bool AddBook(const Book &b) {
        if (FindBook(b.ISBN)) return false;
        Books.push_back(b);
        return true;
    }

    bool RemoveBook(const std::string &isbn) {
        auto it = std::find_if(Books.begin(), Books.end(), [&](const Book &x){ return x.ISBN == isbn; });
        if (it == Books.end()) return false;
        // only remove if available
        if (!it->IsAvailable) return false;
        Books.erase(it);
        return true;
    }

    int NextReaderId() const {
        int maxId = 0;
        for (auto &r : Readers) if (r.Id > maxId) maxId = r.Id;
        return maxId + 1;
    }

    bool AddReader(const Reader &r) {
        if (std::any_of(Readers.begin(), Readers.end(), [&](const Reader &x){ return x.Id == r.Id; })) return false;
        Readers.push_back(r);
        return true;
    }

    bool RemoveReader(int id) {
        auto it = std::find_if(Readers.begin(), Readers.end(), [&](const Reader &x){ return x.Id == id; });
        if (it == Readers.end()) return false;
        // remove active loans
        for (auto lit = Loans.begin(); lit != Loans.end();) {
            if (lit->ReaderId == id && !lit->ReturnDate) lit = Loans.erase(lit);
            else ++lit;
        }
        Readers.erase(it);
        return true;
    }

    bool IssueLoan(const std::string &isbn, int readerId) {
        Book* b = FindBook(isbn);
        if (!b) return false;
        if (!b->IsAvailable) return false;
        if (!FindReader(readerId)) return false;
        Loan ln;
        ln.BookISBN = isbn;
        ln.ReaderId = readerId;
        ln.LoanDate = now_iso();
        ln.ReturnDate = std::nullopt;
        Loans.push_back(ln);
        b->MarkAsLoaned();
        return true;
    }

    bool ReturnBook(const std::string &isbn, int readerId) {
        auto it = std::find_if(Loans.begin(), Loans.end(), [&](const Loan &l){ return l.BookISBN == isbn && l.ReaderId == readerId && !l.ReturnDate; });
        if (it == Loans.end()) return false;
        it->ReturnDate = now_iso();
        Book* b = FindBook(isbn);
        if (b) b->MarkAsAvailable();
        return true;
    }

    std::vector<Book> SearchBooks(const std::string &term) const {
        if (term.empty()) return Books;
        std::string q = Lower(term);
        std::vector<Book> res;
        for (auto &b : Books) {
            if (Lower(b.Title).find(q) != std::string::npos || Lower(b.Author).find(q) != std::string::npos)
                res.push_back(b);
        }
        return res;
    }

    void Save(const std::string &booksFile, const std::string &readersFile, const std::string &loansFile) const {
        json jb = json::array();
        for (auto &b : Books) jb.push_back(b.to_json());
        std::ofstream(booksFile) << jb.dump(4);

        json jr = json::array();
        for (auto &r : Readers) jr.push_back(r.to_json());
        std::ofstream(readersFile) << jr.dump(4);

        json jl = json::array();
        for (auto &l : Loans) jl.push_back(l.to_json());
        std::ofstream(loansFile) << jl.dump(4);
    }

    void Load(const std::string &booksFile, const std::string &readersFile, const std::string &loansFile) {
        Books.clear(); Readers.clear(); Loans.clear();
        try {
            std::ifstream f1(booksFile);
            if (f1) {
                json jb; f1 >> jb;
                for (auto &x : jb) Books.push_back(Book::from_json(x));
            }
            std::ifstream f2(readersFile);
            if (f2) {
                json jr; f2 >> jr;
                for (auto &x : jr) Readers.push_back(Reader::from_json(x));
            }
            std::ifstream f3(loansFile);
            if (f3) {
                json jl; f3 >> jl;
                for (auto &x : jl) Loans.push_back(Loan::from_json(x));
            }
        } catch (...) {
            // ignore errors, start fresh
        }
    }

    std::vector<Book> AvailableBooks() const {
        std::vector<Book> res;
        for (auto &b : Books) if (b.IsAvailable) res.push_back(b);
        return res;
    }
    std::vector<Loan> ActiveLoans() const {
        std::vector<Loan> res;
        for (auto &l : Loans) if (!l.ReturnDate) res.push_back(l);
        return res;
    }

private:
    static std::string Lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return std::tolower(c); });
        return s;
    }

    Book* FindBook(const std::string &isbn) {
        auto it = std::find_if(Books.begin(), Books.end(), [&](const Book &b){ return b.ISBN == isbn; });
        return it == Books.end() ? nullptr : &(*it);
    }
    const Book* FindBook(const std::string &isbn) const {
        auto it = std::find_if(Books.begin(), Books.end(), [&](const Book &b){ return b.ISBN == isbn; });
        return it == Books.end() ? nullptr : &(*it);
    }
    Reader* FindReader(int id) {
        auto it = std::find_if(Readers.begin(), Readers.end(), [&](const Reader &r){ return r.Id == id; });
        return it == Readers.end() ? nullptr : &(*it);
    }
    const Reader* FindReader(int id) const {
        auto it = std::find_if(Readers.begin(), Readers.end(), [&](const Reader &r){ return r.Id == id; });
        return it == Readers.end() ? nullptr : &(*it);
    }
};

void printMenu() {
    std::cout << "\n--- Library Menu ---\n";
    std::cout << "1. Add book\n2. Remove book\n3. Add reader\n4. Remove reader\n5. Issue book\n6. Return book\n7. Search books\n8. Reports\n9. Save & Exit\n0. Exit without save\nChoice: ";
}

int main() {
    LibraryManager mgr;
    const std::string booksFile = "books.json";
    const std::string readersFile = "readers.json";
    const std::string loansFile = "loans.json";

    mgr.Load(booksFile, readersFile, loansFile);
    std::cout << "Library system started. Loaded " << mgr.Books.size() << " books, " << mgr.Readers.size() << " readers.\n";

    while (true) {
        printMenu();
        std::string cmd; std::getline(std::cin, cmd);
        if (cmd == "1") {
            Book b;
            std::cout << "Title: "; std::getline(std::cin, b.Title);
            std::cout << "Author: "; std::getline(std::cin, b.Author);
            std::cout << "ISBN: "; std::getline(std::cin, b.ISBN);
            if (mgr.AddBook(b)) std::cout << "Book added.\n"; else std::cout << "Book exists.\n";
        } else if (cmd == "2") {
            std::cout << "ISBN to remove: "; std::string isbn; std::getline(std::cin, isbn);
            if (mgr.RemoveBook(isbn)) std::cout << "Removed.\n"; else std::cout << "Remove failed (not found or loaned).\n";
        } else if (cmd == "3") {
            Reader r;
            r.Id = mgr.NextReaderId();
            std::cout << "Name: "; std::getline(std::cin, r.Name);
            std::cout << "Email: "; std::getline(std::cin, r.Email);
            mgr.AddReader(r);
            std::cout << "Reader added with Id=" << r.Id << "\n";
        } else if (cmd == "4") {
            std::cout << "Reader id to remove: "; std::string sid; std::getline(std::cin, sid);
            int id = std::stoi(sid);
            if (mgr.RemoveReader(id)) std::cout << "Removed.\n"; else std::cout << "Not found.\n";
        } else if (cmd == "5") {
            std::cout << "ReaderId: "; std::string s; std::getline(std::cin, s); int rid = std::stoi(s);
            std::cout << "ISBN: "; std::string isbn; std::getline(std::cin, isbn);
            if (mgr.IssueLoan(isbn, rid)) std::cout << "Issued.\n"; else std::cout << "Issue failed.\n";
        } else if (cmd == "6") {
            std::cout << "ReaderId: "; std::string s; std::getline(std::cin, s); int rid = std::stoi(s);
            std::cout << "ISBN: "; std::string isbn; std::getline(std::cin, isbn);
            if (mgr.ReturnBook(isbn, rid)) std::cout << "Returned.\n"; else std::cout << "Return failed.\n";
        } else if (cmd == "7") {
            std::cout << "Search term: "; std::string q; std::getline(std::cin, q);
            auto res = mgr.SearchBooks(q);
            for (auto &b : res) {
                std::cout << b.Title << " — " << b.Author << " — " << b.ISBN << " — " << (b.IsAvailable ? "Available" : "Loaned") << "\n";
            }
        } else if (cmd == "8") {
            std::cout << "Available books:\n";
            for (auto &b : mgr.AvailableBooks()) std::cout << b.Title << " — " << b.Author << " — " << b.ISBN << "\n";
            std::cout << "Active loans:\n";
            for (auto &l : mgr.ActiveLoans()) std::cout << "ISBN: " << l.BookISBN << " ReaderId: " << l.ReaderId << " since " << l.LoanDate << "\n";
        } else if (cmd == "9") {
            mgr.Save(booksFile, readersFile, loansFile);
            std::cout << "Saved. Exiting.\n";
            break;
        } else if (cmd == "0") {
            std::cout << "Exit without save.\n";
            break;
        } else {
            std::cout << "Unknown command.\n";
        }
    }

    return 0;
}