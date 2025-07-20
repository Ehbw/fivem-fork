/*
* From https://github.com/dfranx/ImFileDialog commit "0a74b0566e556b1a1f2fa0998cc4309e66f5d36c"
MIT License

Copyright (c) 2021 dfranx

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include <ctime>
#include <stack>
#include <string>
#include <thread>
#include <vector>
#include <functional>
#include <filesystem>
#include <unordered_map>
#include <algorithm> // std::min, std::max

#define IFD_DIALOG_FILE 0
#define IFD_DIALOG_DIRECTORY 1
#define IFD_DIALOG_SAVE 2

namespace ifd
{
class FileDialog
{
public:
	static inline FileDialog& Instance()
	{
		static FileDialog ret;
		return ret;
	}

	FileDialog();
	~FileDialog();

	bool Save(const std::string& key, const std::string& title, const std::string& filter, const std::string& startingDir = "");

	bool Open(const std::string& key, const std::string& title, const std::string& filter, bool isMultiselect = false, const std::string& startingDir = "");

	bool IsDone(const std::string& key);

	inline bool HasResult()
	{
		return m_result.size();
	}
	inline const std::filesystem::path& GetResult()
	{
		return m_result[0];
	}
	inline const std::vector<std::filesystem::path>& GetResults()
	{
		return m_result;
	}

	void Close();

	void RemoveFavorite(const std::string& path);
	void AddFavorite(const std::string& path);
	inline const std::vector<std::string>& GetFavorites()
	{
		return m_favorites;
	}

	inline void SetZoom(float z)
	{
		m_zoom = std::min<float>(25.0f, std::max<float>(1.0f, z));
		m_refreshIconPreview();
	}
	inline float GetZoom()
	{
		return m_zoom;
	}

	std::function<void*(uint8_t*, int, int, char)> CreateTexture; // char -> fmt -> { 0 = BGRA, 1 = RGBA }
	std::function<void(void*)> DeleteTexture;

	class FileTreeNode
	{
	public:
#ifdef _WIN32
		FileTreeNode(const std::wstring& path)
		{
			Path = std::filesystem::path(path);
			Read = false;
		}
#endif

		FileTreeNode(const std::string& path)
		{
			Path = std::filesystem::u8path(path);
			Read = false;
		}

		std::filesystem::path Path;
		bool Read;
		std::vector<FileTreeNode*> Children;
	};
	class FileData
	{
	public:
		FileData(const std::filesystem::path& path);

		std::filesystem::path Path;
		bool IsDirectory;
		size_t Size;
		time_t DateModified;

		bool HasIconPreview;
		void* IconPreview;
		uint8_t* IconPreviewData;
		int IconPreviewWidth, IconPreviewHeight;
	};

private:
	std::string m_currentKey;
	std::string m_currentTitle;
	std::filesystem::path m_currentDirectory;
	bool m_isMultiselect;
	bool m_isOpen;
	uint8_t m_type;
	char m_inputTextbox[1024];
	char m_pathBuffer[1024];
	char m_newEntryBuffer[1024];
	char m_searchBuffer[128];
	std::vector<std::string> m_favorites;
	bool m_calledOpenPopup;
	std::stack<std::filesystem::path> m_backHistory, m_forwardHistory;
	float m_zoom;

	std::vector<std::filesystem::path> m_selections;
	int m_selectedFileItem;
	void m_select(const std::filesystem::path& path, bool isCtrlDown = false);

	std::vector<std::filesystem::path> m_result;
	bool m_finalize(const std::string& filename = "");

	std::string m_filter;
	std::vector<std::vector<std::string>> m_filterExtensions;
	size_t m_filterSelection;
	void m_parseFilter(const std::string& filter);

	std::vector<int> m_iconIndices;
	std::vector<std::string> m_iconFilepaths; // m_iconIndices[x] <-> m_iconFilepaths[x]
	std::unordered_map<std::string, void*> m_icons;
	void* m_getIcon(const std::filesystem::path& path);
	void m_clearIcons();
	void m_refreshIconPreview();
	void m_clearIconPreview();

	std::thread* m_previewLoader;
	bool m_previewLoaderRunning;
	void m_stopPreviewLoader();
	void m_loadPreview();

	std::vector<FileTreeNode*> m_treeCache;
	void m_clearTree(FileTreeNode* node);
	void m_renderTree(FileTreeNode* node);

	unsigned int m_sortColumn;
	unsigned int m_sortDirection;
	std::vector<FileData> m_content;
	void m_setDirectory(const std::filesystem::path& p, bool addHistory = true);
	void m_sortContent(unsigned int column, unsigned int sortDirection);
	void m_renderContent();

	void m_renderPopups();
	void m_renderFileDialog();
};
}
