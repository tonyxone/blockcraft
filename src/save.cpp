#include "save.h"
#include "win_gl.h"

std::string exeDir() {
  char buf[MAX_PATH];
  GetModuleFileNameA(nullptr, buf, MAX_PATH);
  std::string path(buf);
  size_t slash = path.find_last_of("\\/");
  return slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
}

static std::string savesDir() { return exeDir() + "saves\\"; }

static void ensureSavesDir() { CreateDirectoryA(savesDir().c_str(), nullptr); }

static std::string savePath(const std::string& name) {
  return savesDir() + name + ".txt";
}

std::string sanitizeSaveName(const std::string& raw) {
  std::string out;
  for (char c : raw) {
    if (std::isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_') out += c;
  }
  size_t begin = out.find_first_not_of(' ');
  size_t end = out.find_last_not_of(' ');
  if (begin == std::string::npos) return "";
  out = out.substr(begin, end - begin + 1);
  if (out.size() > 24) out = out.substr(0, 24);
  return out;
}

void migrateLegacySave() {
  std::string legacy = exeDir() + "save.txt";
  DWORD attrs = GetFileAttributesA(legacy.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) return;
  std::string dest = savePath("save");
  if (GetFileAttributesA(dest.c_str()) != INVALID_FILE_ATTRIBUTES) return;
  ensureSavesDir();
  MoveFileA(legacy.c_str(), dest.c_str());
}

std::vector<SaveInfo> listSaves() {
  std::vector<std::pair<uint64_t, SaveInfo>> found; // (write time, info)

  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA((savesDir() + "*.txt").c_str(), &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
      std::string file = fd.cFileName;
      if (file.size() <= 4) continue;
      SaveInfo info;
      info.name = file.substr(0, file.size() - 4); // strip ".txt"

      FILETIME local;
      SYSTEMTIME st;
      FileTimeToLocalFileTime(&fd.ftLastWriteTime, &local);
      FileTimeToSystemTime(&local, &st);
      char date[32];
      std::snprintf(date, sizeof(date), "%04d-%02d-%02d %02d:%02d",
                    st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
      info.dateText = date;

      ULARGE_INTEGER t;
      t.LowPart = fd.ftLastWriteTime.dwLowDateTime;
      t.HighPart = fd.ftLastWriteTime.dwHighDateTime;
      found.push_back({ t.QuadPart, info });
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }

  std::sort(found.begin(), found.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<SaveInfo> out;
  for (auto& kv : found) out.push_back(std::move(kv.second));
  return out;
}

bool saveExists(const std::string& name) {
  return GetFileAttributesA(savePath(name).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool deleteSave(const std::string& name) {
  return DeleteFileA(savePath(name).c_str()) != 0;
}

void saveGame(const SaveState& state, const std::string& name) {
  ensureSavesDir();
  FILE* f = std::fopen(savePath(name).c_str(), "w");
  if (!f) return;
  std::fprintf(f, "BLOCKCRAFT_SAVE 1\n");
  std::fprintf(f, "seed %u\n", state.seed);
  std::fprintf(f, "timeOfDay %.17g\n", state.timeOfDay);
  std::fprintf(f, "player %.17g %.17g %.17g %.17g %.17g\n",
               state.x, state.y, state.z, state.yaw, state.pitch);
  std::fprintf(f, "selected %d\n", state.selectedSlot);
  std::fprintf(f, "hotbar");
  for (int c : state.hotbarCounts) std::fprintf(f, " %d", c);
  std::fprintf(f, "\n");
  if (!state.invIds.empty()) {
    // slot count, then that many block ids, then that many counts
    std::fprintf(f, "inventory %d", (int)state.invIds.size());
    for (int id : state.invIds) std::fprintf(f, " %d", id);
    for (int c : state.invCounts) std::fprintf(f, " %d", c);
    std::fprintf(f, "\n");
  }
  std::fprintf(f, "edits %d\n", (int)state.edits.size());
  for (const EditEntry& e : state.edits) {
    std::fprintf(f, "%d %d %d %d\n", e.x, e.y, e.z, (int)e.id);
  }
  std::fprintf(f, "chests %d\n", (int)state.chests.size());
  for (const ChestSaveEntry& c : state.chests) {
    std::fprintf(f, "%d %d %d %d", c.x, c.y, c.z, c.facing);
    for (int id : c.ids) std::fprintf(f, " %d", id);
    for (int count : c.counts) std::fprintf(f, " %d", count);
    std::fprintf(f, "\n");
  }
  std::fprintf(f, "stairs %d\n", (int)state.stairs.size());
  for (const StairSaveEntry& s : state.stairs) {
    std::fprintf(f, "%d %d %d %d\n", s.x, s.y, s.z, s.facing);
  }
  std::fprintf(f, "panels %d\n", (int)state.panels.size());
  for (const PanelSaveEntry& p : state.panels) {
    std::fprintf(f, "%d %d %d %d\n", p.x, p.y, p.z, p.facing);
  }
  std::fprintf(f, "furniture %d\n", (int)state.furniture.size());
  for (const FurnitureSaveEntry& fe : state.furniture) {
    std::fprintf(f, "%d %d %d %d %d %d %d\n",
                 fe.x, fe.y, fe.z, fe.anchorX, fe.anchorY, fe.anchorZ, fe.facing);
  }
  std::fclose(f);
}

bool loadGame(SaveState& out, const std::string& name) {
  FILE* f = std::fopen(savePath(name).c_str(), "r");
  if (!f) return false;
  out = SaveState();

  char magic[32];
  int version = 0;
  if (std::fscanf(f, "%31s %d", magic, &version) != 2 || std::strcmp(magic, "BLOCKCRAFT_SAVE") != 0) {
    std::fclose(f);
    return false;
  }

  char word[32];
  while (std::fscanf(f, "%31s", word) == 1) {
    if (std::strcmp(word, "seed") == 0) {
      unsigned s = 0;
      if (std::fscanf(f, "%u", &s) == 1) out.seed = (uint32_t)s;
    } else if (std::strcmp(word, "timeOfDay") == 0) {
      std::fscanf(f, "%lf", &out.timeOfDay);
    } else if (std::strcmp(word, "player") == 0) {
      if (std::fscanf(f, "%lf %lf %lf %lf %lf", &out.x, &out.y, &out.z, &out.yaw, &out.pitch) == 5) {
        out.hasPlayer = true;
      }
    } else if (std::strcmp(word, "selected") == 0) {
      std::fscanf(f, "%d", &out.selectedSlot);
    } else if (std::strcmp(word, "hotbar") == 0) {
      // counts run until the next keyword; the hotbar now has 10 slots
      // (older saves stop short at 8 and the rest default)
      for (int i = 0; i < 10; i++) {
        int c;
        if (std::fscanf(f, "%d", &c) != 1) break;
        out.hotbarCounts.push_back(c);
      }
    } else if (std::strcmp(word, "inventory") == 0) {
      int n = 0;
      if (std::fscanf(f, "%d", &n) == 1 && n > 0 && n <= 1024) {
        for (int i = 0; i < n; i++) {
          int id;
          if (std::fscanf(f, "%d", &id) != 1) break;
          out.invIds.push_back(id);
        }
        for (int i = 0; i < n; i++) {
          int c;
          if (std::fscanf(f, "%d", &c) != 1) break;
          out.invCounts.push_back(c);
        }
        if (out.invIds.size() != (size_t)n || out.invCounts.size() != (size_t)n) {
          out.invIds.clear(); // truncated line: ignore the whole section
          out.invCounts.clear();
        }
      }
    } else if (std::strcmp(word, "edits") == 0) {
      int n = 0;
      std::fscanf(f, "%d", &n);
      for (int i = 0; i < n; i++) {
        EditEntry e;
        int id;
        if (std::fscanf(f, "%d %d %d %d", &e.x, &e.y, &e.z, &id) != 4) break;
        e.id = (uint8_t)id;
        out.edits.push_back(e);
      }
    } else if (std::strcmp(word, "chests") == 0) {
      int n = 0;
      std::fscanf(f, "%d", &n);
      for (int i = 0; i < n; i++) {
        ChestSaveEntry c;
        bool ok = std::fscanf(f, "%d %d %d %d", &c.x, &c.y, &c.z, &c.facing) == 4;
        for (int& id : c.ids) ok = ok && std::fscanf(f, "%d", &id) == 1;
        for (int& count : c.counts) ok = ok && std::fscanf(f, "%d", &count) == 1;
        if (!ok) break;
        out.chests.push_back(c);
      }
    } else if (std::strcmp(word, "stairs") == 0) {
      int n = 0;
      std::fscanf(f, "%d", &n);
      for (int i = 0; i < n; i++) {
        StairSaveEntry s;
        if (std::fscanf(f, "%d %d %d %d", &s.x, &s.y, &s.z, &s.facing) != 4) break;
        out.stairs.push_back(s);
      }
    } else if (std::strcmp(word, "panels") == 0) {
      int n = 0;
      std::fscanf(f, "%d", &n);
      for (int i = 0; i < n; i++) {
        PanelSaveEntry p;
        if (std::fscanf(f, "%d %d %d %d", &p.x, &p.y, &p.z, &p.facing) != 4) break;
        out.panels.push_back(p);
      }
    } else if (std::strcmp(word, "furniture") == 0) {
      int n = 0;
      std::fscanf(f, "%d", &n);
      for (int i = 0; i < n; i++) {
        FurnitureSaveEntry fe;
        if (std::fscanf(f, "%d %d %d %d %d %d %d", &fe.x, &fe.y, &fe.z,
                        &fe.anchorX, &fe.anchorY, &fe.anchorZ, &fe.facing) != 7) break;
        out.furniture.push_back(fe);
      }
    }
  }
  std::fclose(f);
  return true;
}
