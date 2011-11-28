
#include <fstream>
#include <iostream>
#include <iomanip>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <vector>
#include <bitset>
#include <ctime>
#include <map>

#include <boost/shared_ptr.hpp>
#include <boost/foreach.hpp>
#include <boost/ref.hpp>
#include <boost/make_shared.hpp>
#include <boost/date_time/posix_time/ptime.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/iostreams/copy.hpp>

#include "loader/offsets.hpp"

#include "setup/data.hpp"
#include "setup/delete.hpp"
#include "setup/directory.hpp"
#include "setup/file.hpp"
#include "setup/MessageEntry.hpp"
#include "setup/IconEntry.hpp"
#include "setup/IniEntry.hpp"
#include "setup/LanguageEntry.hpp"
#include "setup/PermissionEntry.hpp"
#include "setup/RegistryEntry.hpp"
#include "setup/RunEntry.hpp"
#include "setup/SetupComponentEntry.hpp"
#include "setup/SetupHeader.hpp"
#include "setup/SetupTaskEntry.hpp"
#include "setup/SetupTypeEntry.hpp"
#include "setup/version.hpp"

#include "stream/block.hpp"
#include "stream/chunk.hpp"
#include "stream/file.hpp"
#include "stream/slice.hpp"

#include "util/console.hpp"
#include "util/load.hpp"
#include "util/log.hpp"
#include "util/output.hpp"

using std::cout;
using std::string;
using std::endl;
using std::setw;
using std::setfill;

namespace io = boost::iostreams;
namespace fs = boost::filesystem;

struct FileLocationComparer {
	
	const std::vector<setup::data_entry> & locations;
	
	explicit FileLocationComparer(const std::vector<setup::data_entry> & loc) : locations(loc) { }
	FileLocationComparer(const FileLocationComparer & o) : locations(o.locations) { }
	
	bool operator()(size_t a, size_t b) {
		return (locations[a].file_offset < locations[b].file_offset);
	}
	
};

static void print(std::ostream & os, const SetupItem & item, const SetupHeader & header) {
	
	os << if_not_empty("  Componenets", item.components);
	os << if_not_empty("  Tasks", item.tasks);
	os << if_not_empty("  Languages", item.languages);
	os << if_not_empty("  Check", item.check);
	
	os << if_not_empty("  After install", item.afterInstall);
	os << if_not_empty("  Before install", item.beforeInstall);
	
	os << if_not_equal("  Min version", item.minVersion, header.minVersion);
	os << if_not_equal("  Only below version", item.onlyBelowVersion, header.onlyBelowVersion);
	
}

static void print(std::ostream & os, const RunEntry & entry, const SetupHeader & header) {
	
	os << " - " << quoted(entry.name) << ':' << endl;
	os << if_not_empty("  Parameters", entry.parameters);
	os << if_not_empty("  Working directory", entry.workingDir);
	os << if_not_empty("  Run once id", entry.runOnceId);
	os << if_not_empty("  Status message", entry.statusMessage);
	os << if_not_empty("  Verb", entry.verb);
	os << if_not_empty("  Description", entry.verb);
	
	print(cout, static_cast<const SetupItem &>(entry), header);
	
	os << if_not_equal("  Show command", entry.showCmd, 1);
	os << if_not_equal("  Wait", entry.wait, RunEntry::WaitUntilTerminated);
	
	os << if_not_zero("  Options", entry.options);
	
}

static const char * magicNumbers[][2] = {
	{ "GIF89a", "gif" },
	{ "GIF87a", "gif" },
	{ "\xFF\xD8", "jpg" },
	{ "\x89PNG\r\n\x1A\n", "png" },
	{ "%PDF", "pdf" },
	{ "MZ", "dll" },
	{ "BM", "bmp" },
};

static const char * guessExtension(const string & data) {
	
	for(size_t i = 0; i < ARRAY_SIZE(magicNumbers); i++) {
		
		size_t n = strlen(magicNumbers[i][0]);
		
		if(!data.compare(0, n, magicNumbers[i][0], n)) {
			return magicNumbers[i][1];
		}
	}
	
	return "bin";
}

static void dump(std::istream & is, const string & file) {
	
	// TODO stream
	
	std::string data;
	is >> binary_string(data);
	cout << "Resource: " << color::cyan << file << color::reset << ": " << color::white
	     << data.length() << color::reset << " bytes" << endl;
	
	if(data.empty()) {
		return;
	}
	
	std::string filename = file + '.' + guessExtension(data);
	
	std::ofstream ofs(filename.c_str(), std::ios_base::trunc | std::ios_base::binary
	                                    | std::ios_base::out);
	
	ofs << data;
};

static void readWizardImageAndDecompressor(std::istream & is, const inno_version & version,
                                           const SetupHeader & header) {
	
	cout << endl;
	
	dump(is, "wizard");
	
	if(version >= INNO_VERSION(2, 0, 0)) {
		dump(is, "wizard_small");
	}
	
	if(header.compressMethod == stream::chunk::BZip2
	   || (header.compressMethod == stream::chunk::LZMA1 && version == INNO_VERSION(4, 1, 5))
	   || (header.compressMethod == stream::chunk::Zlib && version >= INNO_VERSION(4, 2, 6))) {
		
		dump(is, "decompressor");
	}
	
	if(is.fail()) {
		log_error << "error reading misc setup data";
	}
	
}

int main(int argc, char * argv[]) {
	
	color::init();
	
	logger::debug = true;
	
	if(argc <= 1) {
		std::cout << "usage: innoextract <Inno Setup installer>" << endl;
		return 1;
	}
	
	std::ifstream ifs(argv[1], std::ios_base::in | std::ios_base::binary | std::ios_base::ate);
	
	if(!ifs.is_open()) {
		log_error << "error opening file";
		return 1;
	}
	
	loader::offsets offsets;
	offsets.load(ifs);
	
	cout << std::boolalpha;
	
	cout << "loaded offsets:" << endl;
	if(offsets.exe_offset) {
		cout << "- exe: @ " << color::cyan << print_hex(offsets.exe_offset) << color::reset;
		if(offsets.exe_compressed_size) {
			cout << "  compressed: " << color::cyan << print_hex(offsets.exe_compressed_size)
			     << color::reset;
		}
		cout << "  uncompressed: " << color::cyan << print_bytes(offsets.exe_uncompressed_size)
		     << color::reset << endl;
		cout << "- exe checksum: " << color::cyan << offsets.exe_checksum  << color::reset << endl;
	}
	cout << if_not_zero("- message offset", print_hex(offsets.message_offset));
	cout << "- header offset: " << color::cyan << print_hex(offsets.header_offset)
	     << color::reset << endl;
	cout << if_not_zero("- data offset", print_hex(offsets.data_offset));
	
	ifs.seekg(offsets.header_offset);
	
	inno_version version;
	version.load(ifs);
	if(ifs.fail()) {
		log_error << "error reading setup data version!";
		return 1;
	}
	
	if(!version.known) {
		log_error << "unknown version!";
		return 1; // TODO
	}
	
	cout << "version: " << color::white << version << color::reset << endl;
	
	stream::block_reader::pointer is = stream::block_reader::get(ifs, version);
	if(!is) {
		log_error << "error reading block";
		return 1;
	}
	
	is->exceptions(std::ios_base::badbit | std::ios_base::failbit);
	
	SetupHeader header;
	header.load(*is, version);
	if(is->fail()) {
		log_error << "error reading setup data header!";
		return 1;
	}
	
	cout << endl;
	
	cout << if_not_empty("App name", header.appName);
	cout << if_not_empty("App ver name", header.appVerName);
	cout << if_not_empty("App id", header.appId);
	cout << if_not_empty("Copyright", header.appCopyright);
	cout << if_not_empty("Publisher", header.appPublisher);
	cout << if_not_empty("Publisher URL", header.appPublisherURL);
	cout << if_not_empty("Support phone", header.appSupportPhone);
	cout << if_not_empty("Support URL", header.appSupportURL);
	cout << if_not_empty("Updates URL", header.appUpdatesURL);
	cout << if_not_empty("Version", header.appVersion);
	cout << if_not_empty("Default dir name", header.defaultDirName);
	cout << if_not_empty("Default group name", header.defaultGroupName);
	cout << if_not_empty("Uninstall icon name", header.uninstallIconName);
	cout << if_not_empty("Base filename", header.baseFilename);
	cout << if_not_empty("Uninstall files dir", header.uninstallFilesDir);
	cout << if_not_empty("Uninstall display name", header.uninstallDisplayName);
	cout << if_not_empty("Uninstall display icon", header.uninstallDisplayIcon);
	cout << if_not_empty("App mutex", header.appMutex);
	cout << if_not_empty("Default user name", header.defaultUserInfoName);
	cout << if_not_empty("Default user org", header.defaultUserInfoOrg);
	cout << if_not_empty("Default user serial", header.defaultUserInfoSerial);
	cout << if_not_empty("Readme", header.appReadmeFile);
	cout << if_not_empty("Contact", header.appContact);
	cout << if_not_empty("Comments", header.appComments);
	cout << if_not_empty("Modify path", header.appModifyPath);
	cout << if_not_empty("Uninstall reg key", header.createUninstallRegKey);
	cout << if_not_empty("Uninstallable", header.uninstallable);
	cout << if_not_empty("License", header.licenseText);
	cout << if_not_empty("Info before text", header.infoBeforeText);
	cout << if_not_empty("Info after text", header.infoAfterText);
	cout << if_not_empty("Uninstaller signature", header.signedUninstallerSignature);
	cout << if_not_empty("Compiled code", header.compiledCodeText);
	
	cout << if_not_zero("Lead bytes", header.leadBytes);
	
	cout << if_not_zero("Language entries", header.numLanguageEntries);
	cout << if_not_zero("Custom message entries", header.numCustomMessageEntries);
	cout << if_not_zero("Permission entries", header.numPermissionEntries);
	cout << if_not_zero("Type entries", header.numTypeEntries);
	cout << if_not_zero("Component entries", header.numComponentEntries);
	cout << if_not_zero("Task entries", header.numTaskEntries);
	cout << if_not_zero("Dir entries", header.numDirectoryEntries);
	cout << if_not_zero("File entries", header.numFileEntries);
	cout << if_not_zero("File location entries", header.numFileLocationEntries);
	cout << if_not_zero("Icon entries", header.numIconEntries);
	cout << if_not_zero("Ini entries", header.numIniEntries);
	cout << if_not_zero("Registry entries", header.numRegistryEntries);
	cout << if_not_zero("Delete entries", header.numDeleteEntries);
	cout << if_not_zero("Uninstall delete entries", header.numUninstallDeleteEntries);
	cout << if_not_zero("Run entries", header.numRunEntries);
	cout << if_not_zero("Uninstall run entries", header.numUninstallRunEntries);
	
	cout << if_not_equal("Min version", header.minVersion, WindowsVersion::none);
	cout << if_not_equal("Only below version", header.onlyBelowVersion, WindowsVersion::none);
	
	cout << std::hex;
	cout << if_not_zero("Back color", header.backColor);
	cout << if_not_zero("Back color2", header.backColor2);
	cout << if_not_zero("Wizard image back color", header.wizardImageBackColor);
	cout << if_not_zero("Wizard small image back color", header.wizardSmallImageBackColor);
	cout << std::dec;
	
	if(header.options & (SetupHeader::Password | SetupHeader::EncryptionUsed)) {
		cout << "Password: " << color::cyan << header.password << color::reset << endl;
		// TODO print salt
	}
	
	cout << if_not_zero("Extra disk space required", header.extraDiskSpaceRequired);
	cout << if_not_zero("Slices per disk", header.slicesPerDisk);
	
	cout << if_not_equal("Install mode", header.installMode, SetupHeader::NormalInstallMode);
	cout << "Uninstall log mode: " << color::cyan << header.uninstallLogMode
	     << color::reset << endl;
	cout << "Uninstall style: " << color::cyan << header.uninstallStyle << color::reset << endl;
	cout << "Dir exists warning: " << color::cyan << header.dirExistsWarning
	     << color::reset << endl;
	cout << if_not_equal("Privileges required", header.privilegesRequired, SetupHeader::NoPrivileges);
	cout << "Show language dialog: " << color::cyan << header.showLanguageDialog
	     << color::reset << endl;
	cout << if_not_equal("Danguage detection", header.languageDetectionMethod,
	              SetupHeader::NoLanguageDetection);
	cout << "Compression: " << color::cyan << header.compressMethod << color::reset << endl;
	cout << "Architectures allowed: " << color::cyan << header.architecturesAllowed
	     << color::reset << endl;
	cout << "Architectures installed in 64-bit mode: " << color::cyan
	     << header.architecturesInstallIn64BitMode << color::reset << endl;
	
	if(header.options & SetupHeader::SignedUninstaller) {
		cout << if_not_zero("Size before signing uninstaller", header.signedUninstallerOrigSize);
		cout << if_not_zero("Uninstaller header checksum", header.signedUninstallerHdrChecksum);
	}
	
	cout << "Disable dir page: " << color::cyan << header.disableDirPage << color::reset << endl;
	cout << "Disable program group page: " << color::cyan << header.disableProgramGroupPage
	     << color::reset << endl;
	
	cout << if_not_zero("Uninstall display size", header.uninstallDisplaySize);
	
	cout << "Options: " << color::green << header.options << color::reset << endl;
	
	cout << color::reset;
	
	if(header.numLanguageEntries) {
		cout << endl << "Language entries:" << endl;
	}
	std::vector<LanguageEntry> languages;
	languages.resize(header.numLanguageEntries);
	for(size_t i = 0; i < header.numLanguageEntries; i++) {
		
		LanguageEntry & entry = languages[i];
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading language entry #" << i;
		}
		
		cout << " - " << quoted(entry.name) << ':' << endl;
		cout << if_not_empty("  Language name", entry.languageName);
		cout << if_not_empty("  Dialog font", entry.dialogFontName);
		cout << if_not_empty("  Title font", entry.titleFontName);
		cout << if_not_empty("  Welcome font", entry.welcomeFontName);
		cout << if_not_empty("  Copyright font", entry.copyrightFontName);
		cout << if_not_empty("  Data", entry.data);
		cout << if_not_empty("  License", entry.licenseText);
		cout << if_not_empty("  Info before text", entry.infoBeforeText);
		cout << if_not_empty("  Info after text", entry.infoAfterText);
		
		cout << "  Language id: " << color::cyan << std::hex << entry.languageId << std::dec
		     << color::reset << endl;
		
		cout << if_not_zero("  Codepage", entry.codepage);
		cout << if_not_zero("  Dialog font size", entry.dialogFontSize);
		cout << if_not_zero("  Dialog font standard height", entry.dialogFontStandardHeight);
		cout << if_not_zero("  Title font size", entry.titleFontSize);
		cout << if_not_zero("  Welcome font size", entry.welcomeFontSize);
		cout << if_not_zero("  Copyright font size", entry.copyrightFontSize);
		cout << if_not_equal("  Right to left", entry.rightToLeft, false);
		
	};
	
	if(version < INNO_VERSION(4, 0, 0)) {
		readWizardImageAndDecompressor(*is, version, header);
	}
	
	if(header.numCustomMessageEntries) {
		cout << endl << "Message entries:" << endl;
	}
	for(size_t i = 0; i < header.numCustomMessageEntries; i++) {
		
		MessageEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading custom message entry #" << i;
		}
		
		if(entry.language >= 0 ? size_t(entry.language) >= languages.size() : entry.language != -1) {
			log_warning << "unexpected language index: " << entry.language;
		}
		
		uint32_t codepage;
		if(entry.language < 0) {
			codepage = version.codepage();
		} else {
			codepage = languages[size_t(entry.language)].codepage;
		}
		
		string decoded;
		to_utf8(entry.value, decoded, codepage);
		
		cout << " - " << quoted(entry.name);
		if(entry.language < 0) {
			cout << " (default) = ";
		} else {
			cout << " (" << color::cyan << languages[size_t(entry.language)].name
			     << color::reset << ") = ";
		}
		cout << quoted(decoded) << endl;
		
	}
	
	if(header.numPermissionEntries) {
		cout << endl << "Permission entries:" << endl;
	}
	for(size_t i = 0; i < header.numPermissionEntries; i++) {
		
		PermissionEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading permission entry #" << i;
		}
		
		cout << " - " << entry.permissions.length() << " bytes";
		
	}
	
	if(header.numTypeEntries) {
		cout << endl << "Type entries:" << endl;
	}
	for(size_t i = 0; i < header.numTypeEntries; i++) {
		
		SetupTypeEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading type entry #" << i;
		}
		
		cout << " - " << quoted(entry.name) << ':' << endl;
		cout << if_not_empty("  Description", entry.description);
		cout << if_not_empty("  Languages", entry.languages);
		cout << if_not_empty("  Check", entry.check);
		
		cout << if_not_equal("  Min version", entry.minVersion, header.minVersion);
		cout << if_not_equal("  Only below version", entry.onlyBelowVersion, header.onlyBelowVersion);
		
		cout << if_not_zero("  Options", entry.options);
		cout << if_not_equal("  Type", entry.type, SetupTypeEntry::User);
		cout << if_not_zero("  Size", entry.size);
		
	}
	
	if(header.numComponentEntries) {
		cout << endl << "Component entries:" << endl;
	}
	for(size_t i = 0; i < header.numComponentEntries; i++) {
		
		SetupComponentEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading component entry #" << i;
		}
		
		cout << " - " << quoted(entry.name) << ':' << endl;
		cout << if_not_empty("  Types", entry.types);
		cout << if_not_empty("  Description", entry.description);
		cout << if_not_empty("  Languages", entry.languages);
		cout << if_not_empty("  Check", entry.check);
		
		cout << if_not_zero("  Extra disk space required", entry.extraDiskSpaceRequired);
		cout << if_not_zero("  Level", entry.level);
		cout << if_not_equal("  Used", entry.used, true);
		
		cout << if_not_equal("  Min version", entry.minVersion, header.minVersion);
		cout << if_not_equal("  Only below version", entry.onlyBelowVersion, header.onlyBelowVersion);
		
		cout << if_not_zero("  Options", entry.options);
		cout << if_not_zero("  Size", entry.size);
		
	}
	
	if(header.numTaskEntries) {
		cout << endl << "Task entries:" << endl;
	}
	for(size_t i = 0; i < header.numTaskEntries; i++) {
		
		SetupTaskEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading task entry #" << i;
		}
		
		cout << " - " << quoted(entry.name) << ':' << endl;
		cout << if_not_empty("  Description", entry.description);
		cout << if_not_empty("  Group description", entry.groupDescription);
		cout << if_not_empty("  Components", entry.components);
		cout << if_not_empty("  Languages", entry.languages);
		cout << if_not_empty("  Check", entry.check);
		
		cout << if_not_zero("  Level", entry.level);
		cout << if_not_equal("  Used", entry.used, true);
		
		cout << if_not_equal("  Min version", entry.minVersion, header.minVersion);
		cout << if_not_equal("  Only below version", entry.onlyBelowVersion, header.onlyBelowVersion);
		
		cout << if_not_zero("  Options", entry.options);
		
	}
	
	if(header.numDirectoryEntries) {
		cout << endl << "Directory entries:" << endl;
	}
	for(size_t i = 0; i < header.numDirectoryEntries; i++) {
		
		setup::directory_entry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading directory entry #" << i;
		}
		
		cout << " - " << quoted(entry.name) << ':' << endl;
		
		print(cout, entry, header);
		
		if(!entry.permissions.empty()) {
			cout << "  Permissions: " << entry.permissions.length() << " bytes";
		}
		
		
		cout << if_not_zero("  Attributes", entry.attributes);
		
		cout << if_not_equal("  Permission entry", entry.permission, -1);
		
		cout << if_not_zero("  Options", entry.options);
		
	}
	
	if(header.numFileEntries) {
		cout << endl << "File entries:" << endl;
	}
	std::vector<setup::file_entry> files;
	files.resize(header.numFileEntries);
	for(size_t i = 0; i < header.numFileEntries; i++) {
		
		setup::file_entry & entry = files[i];
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading file entry #" << i;
		}
		
		if(entry.destination.empty()) {
			cout << " - File #" << i;
		} else {
			cout << " - " << quoted(entry.destination);
		}
		if(entry.location != uint32_t(-1)) {
			cout << " (location: " << color::cyan << entry.location << color::reset << ')';
		}
		cout  << endl;
		
		cout << if_not_empty("  Source", entry.source);
		cout << if_not_empty("  Install font name", entry.install_font_name);
		cout << if_not_empty("  Strong assembly name", entry.strong_assembly_name);
		
		print(cout, entry, header);
		
		cout << if_not_zero("  Attributes", entry.attributes);
		cout << if_not_zero("  Size", entry.external_size);
		
		cout << if_not_equal("  Permission entry", entry.permission, -1);
		
		cout << if_not_zero("  Options", entry.options);
		
		cout << if_not_equal("  Type", entry.type, setup::file_entry::UserFile);
		
	}
	
	if(header.numIconEntries) {
		cout << endl << "Icon entries:" << endl;
	}
	for(size_t i = 0; i < header.numIconEntries; i++) {
		
		IconEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading icon entry #" << i;
		}
		
		cout << " - " << quoted(entry.name) << " -> " << quoted(entry.filename) << endl;
		cout << if_not_empty("  Parameters", entry.parameters);
		cout << if_not_empty("  Working directory", entry.workingDir);
		cout << if_not_empty("  Icon file", entry.iconFilename);
		cout << if_not_empty("  Comment", entry.comment);
		cout << if_not_empty("  App user model id", entry.appUserModelId);
		
		print(cout, entry, header);
		
		cout << if_not_zero("  Icon index", entry.iconIndex);
		cout << if_not_equal("  Show command", entry.showCmd, 1);
		cout << if_not_equal("  Close on exit", entry.closeOnExit, IconEntry::NoSetting);
		
		cout << if_not_zero("  Hotkey", entry.hotkey);
		
		cout << if_not_zero("  Options", entry.options);
		
	}
	
	if(header.numIniEntries) {
		cout << endl << "Ini entries:" << endl;
	}
	for(size_t i = 0; i < header.numIniEntries; i++) {
		
		IniEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading ini entry #" << i;
		}
		
		cout << " - in " << quoted(entry.inifile);
		cout << " set [" << quoted(entry.section) << "] ";
		cout << quoted(entry.key) << " = " << quoted(entry.value) << std::endl;
		
		print(cout, entry, header);
		
		cout << if_not_zero("  Options", entry.options);
		
	}
	
	if(header.numRegistryEntries) {
		cout << endl << "Registry entries:" << endl;
	}
	for(size_t i = 0; i < header.numRegistryEntries; i++) {
		
		RegistryEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading registry entry #" << i;
		}
		
		cout << " - ";
		if(entry.hive != RegistryEntry::Unset) {
			cout << entry.hive << '\\';
		}
		cout << quoted(entry.key);
		cout << endl << "  ";
		if(entry.name.empty()) {
			cout << "(default)";
		} else {
			cout << quoted(entry.name);
		}
		if(!entry.value.empty()) {
			cout << " = " << quoted(entry.value);
		}
		if(entry.type != RegistryEntry::None) {
			cout << " (" << color::cyan << entry.type << color::reset << ')';
		}
		cout << endl;
		
		print(cout, entry, header);
		
		if(!entry.permissions.empty()) {
			cout << "  Permissions: " << entry.permissions.length() << " bytes";
		}
		cout << if_not_equal("  Permission entry", entry.permission, -1);
		
		cout << if_not_zero("  Options", entry.options);
		
	}
	
	if(header.numDeleteEntries) {
		cout << endl << "Delete entries:" << endl;
	}
	for(size_t i = 0; i < header.numDeleteEntries; i++) {
		
		setup::delete_entry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading install delete entry #" << i;
		}
		
		cout << " - " << quoted(entry.name)
		     << " (" << color::cyan << entry.type << color::reset << ')' << endl;
		
		print(cout, entry, header);
		
	}
	
	if(header.numUninstallDeleteEntries) {
		cout << endl << "Uninstall delete entries:" << endl;
	}
	for(size_t i = 0; i < header.numUninstallDeleteEntries; i++) {
		
		setup::delete_entry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading uninstall delete entry #" << i;
		}
		
		cout << " - " << quoted(entry.name)
		     << " (" << color::cyan << entry.type << color::reset << ')' << endl;
		
		print(cout, entry, header);
		
	}
	
	if(header.numRunEntries) {
		cout << endl << "Run entries:" << endl;
	}
	for(size_t i = 0; i < header.numRunEntries; i++) {
		
		RunEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading install run entry #" << i;
		}
		
		print(cout, entry, header);
		
	}
	
	if(header.numUninstallRunEntries) {
		cout << endl << "Uninstall run entries:" << endl;
	}
	for(size_t i = 0; i < header.numUninstallRunEntries; i++) {
		
		RunEntry entry;
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading uninstall run entry #" << i;
		}
		
		print(cout, entry, header);
		
	}
	
	if(version >= INNO_VERSION(4, 0, 0)) {
		readWizardImageAndDecompressor(*is, version, header);
	}
	
	{
		is->exceptions(std::ios_base::goodbit);
		char dummy;
		if(!is->get(dummy).eof()) {
			log_warning << "expected end of stream";
		}
	}
	
	// TODO skip to end if not there yet
	
	is = stream::block_reader::get(ifs, version);
	if(!is) {
		log_error << "error reading block";
		return 1;
	}
	
	is->exceptions(std::ios_base::badbit | std::ios_base::failbit);
	
	if(header.numFileLocationEntries) {
		cout << endl << "File location entries:" << endl;
	}
	std::vector<setup::data_entry> locations;
	locations.resize(header.numFileLocationEntries);
	for(size_t i = 0; i < header.numFileLocationEntries; i++) {
		
		setup::data_entry & entry = locations[i];
		entry.load(*is, version);
		if(is->fail()) {
			log_error << "error reading file location entry #" << i;
		}
		
		cout << " - " << "File location #" << i << ':' << endl;
		
		cout << if_not_zero("  First slice", entry.first_slice);
		cout << if_not_equal("  Last slice", entry.last_slice, entry.first_slice);
		
		cout << "  Chunk: offset " << color::cyan << print_hex(entry.chunk_offset) << color::reset
		     << " size " << color::cyan << print_hex(entry.chunk_size) << color::reset << std::endl;
		
		cout << if_not_zero("  File offset", print_hex(entry.file_offset));
		cout << if_not_zero("  File size", print_bytes(entry.file_size));
		
		cout << "  Checksum: " << entry.checksum << endl;
		
		std::tm t;
		if(entry.options & setup::data_entry::TimeStampInUTC) {
			gmtime_r(&entry.timestamp.tv_sec, &t);
		} else {
			localtime_r(&entry.timestamp.tv_sec, &t);
		}
		
		cout << "  Timestamp: " << color::cyan << (t.tm_year + 1900)
		     << '-' << std::setfill('0') << std::setw(2) << (t.tm_mon + 1)
		     << '-' << std::setfill('0') << std::setw(2) << t.tm_mday
		     << ' ' << std::setfill(' ') << std::setw(2) << t.tm_hour
		     << ':' << std::setfill('0') << std::setw(2) << t.tm_min
		     << ':' << std::setfill('0') << std::setw(2) << t.tm_sec
		     << color::reset << " +" << entry.timestamp.tv_nsec << endl;
		
		cout << if_not_zero("  Options", entry.options);
		
		if(entry.options & setup::data_entry::VersionInfoValid) {
			cout << if_not_zero("  File version LS", entry.file_version_ls);
			cout << if_not_zero("  File version MS", entry.file_version_ms);
		}
		
	}
	
	{
		is->exceptions(std::ios_base::goodbit);
		char dummy;
		if(!is->get(dummy).eof()) {
			log_warning << "expected end of stream";
		}
	}
	
	is.reset();
	
	std::vector<std::vector<size_t> > files_for_location;
	files_for_location.resize(locations.size());
	for(size_t i = 0; i < files.size(); i++) {
		if(files[i].location < files_for_location.size()) {
			files_for_location[files[i].location].push_back(i);
		}
	}
	
	typedef std::map<stream::chunk, std::vector<size_t> > Chunks;
	Chunks chunks;
	for(size_t i = 0; i < locations.size(); i++) {
		const setup::data_entry & location = locations[i];
		
		stream::chunk::compression_method compression = stream::chunk::Stored;
		if(location.options & setup::data_entry::ChunkCompressed) {
			compression = header.compressMethod;
		}
		
		chunks[stream::chunk(location.first_slice, location.chunk_offset, location.chunk_size,
		                     compression, location.options & setup::data_entry::ChunkEncrypted)
		      ].push_back(i);
		assert(header.compressMethod == stream::chunk::BZip2
		       || !(location.options & setup::data_entry::BZipped));
	}
	
	boost::shared_ptr<stream::slice_reader> slice_reader;
	
	if(offsets.data_offset) {
		slice_reader = boost::make_shared<stream::slice_reader>(argv[1], offsets.data_offset);
	} else {
		fs::path path(argv[1]);
		slice_reader = boost::make_shared<stream::slice_reader>(path.parent_path().string() + '/',
		                                                        path.stem().string(),
		                                                        header.slicesPerDisk);
	}
	
	try {
	
	BOOST_FOREACH(Chunks::value_type & chunk, chunks) {
		
		cout << "[starting " << chunk.first.compression
		     << " chunk @ " << chunk.first.first_slice << " + " << print_hex(offsets.data_offset)
		     << " + " << print_hex(chunk.first.offset) << ']' << std::endl;
		
		std::sort(chunk.second.begin(), chunk.second.end(), FileLocationComparer(locations));
		
		stream::chunk_reader::pointer chunk_source;
		chunk_source = stream::chunk_reader::get(*slice_reader, chunk.first);
		
		uint64_t offset = 0;
		
		BOOST_FOREACH(size_t location_i, chunk.second) {
			const setup::data_entry & location = locations[location_i];
			
			if(location.file_offset < offset) {
				log_error << "bad offset";
				return 1;
			}
			
			if(location.file_offset > offset) {
				std::cout << "discarding " << print_bytes(location.file_offset - offset) << std::endl;
				discard(*chunk_source, location.file_offset - offset);
			}
			offset = location.file_offset + location.file_size;
			
			std::cout << "-> reading ";
			bool named = false;
			BOOST_FOREACH(size_t file_i, files_for_location[location_i]) {
				if(!files[file_i].destination.empty()) {
					std::cout << '"' << files[file_i].destination << '"';
					named = true;
					break;
				}
			}
			if(!named) {
				std::cout << "unnamed file";
			}
			std::cout << " @ " << print_hex(location.file_offset)
			          << " (" << print_bytes(location.file_size) << ')' << std::endl;
			
			crypto::checksum checksum;
			
			stream::file_reader::pointer file_source;
			file_source = stream::file_reader::get(*chunk_source, location, version, &checksum);
			
			BOOST_FOREACH(size_t file_i, files_for_location[location_i]) {
				if(!files[file_i].destination.empty()) {
					std::ofstream ofs(files[file_i].destination.c_str());
					
					char buffer[8192 * 10];
					
					float status = 0.f;
					uint64_t total = 0;
					
					std::ostringstream oss;
					float last_rate = 0;
					
					int64_t last_milliseconds = 0;
					
					boost::posix_time::ptime start(boost::posix_time::microsec_clock::universal_time());
					
					while(!file_source->eof()) {
						
						std::streamsize n = file_source->read(buffer, ARRAY_SIZE(buffer)).gcount();
						
						if(n > 0) {
							
							ofs.write(buffer, n);
							
							total += uint64_t(n);
							float new_status = float(size_t(1000.f * float(total) / float(location.file_size)))
							                   * (1 / 1000.f);
							if(status != new_status && new_status != 100.f) {
								
								boost::posix_time::ptime now(boost::posix_time::microsec_clock::universal_time());
								int64_t milliseconds = (now - start).total_milliseconds();
								
								if(milliseconds - last_milliseconds > 200) {
									last_milliseconds = milliseconds;
									
									if(total >= 10 * 1024 && milliseconds > 0) {
										float rate = 1000.f * float(total) / float(milliseconds);
										if(rate != last_rate) {
											last_rate = rate;
											oss.str(string()); // clear the buffer
											oss << std::right << std::fixed << std::setfill(' ') << std::setw(8)
											    << print_bytes(rate) << "/s";
										}
									}
									
									status = new_status;
									progress::show(status, oss.str());
								}
							}
						}
					}
					
					break; // TODO ...
				}
			}
			
			progress::clear();
			
			if(checksum != location.checksum) {
				log_warning << "checksum mismatch:";
				log_warning << "actual:   " << checksum;
				log_warning << "expected: " << location.checksum;
			}
		}
	}
	
	} catch(std::ios_base::failure e) {
		log_error << e.what();
	}
	
	std::cout << color::green << "Done" << color::reset << std::dec;
	
	if(logger::total_errors || logger::total_warnings) {
		std::cout << " with ";
		if(logger::total_errors) {
			std::cout << color::red << logger::total_errors << " errors" << color::reset;
		}
		if(logger::total_errors && logger::total_warnings) {
			std::cout << " and ";
		}
		if(logger::total_warnings) {
			std::cout << color::yellow << logger::total_warnings << " warnings" << color::reset;
		}
	}
	
	std::cout << '.' << std::endl;
	
	return 0;
}
