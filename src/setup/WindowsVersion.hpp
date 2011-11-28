
#ifndef INNOEXTRACT_SETUP_WINDOWSVERSION_HPP
#define INNOEXTRACT_SETUP_WINDOWSVERSION_HPP

#include <ostream>
#include "setup/version.hpp"

struct WindowsVersion {
	
	struct Version {
		
		unsigned major;
		unsigned minor;
		unsigned build;
	
		inline bool operator==(const Version & o) const {
			return (build == o.build && major == o.major && minor == o.minor);
		}
		
		inline bool operator!=(const Version & o) const {
			return !(*this == o);
		}
		
		void load(std::istream & is, const inno_version & version);
		
	};
	
	Version winVersion;
	Version ntVersion;
	
	struct ServicePack {
		
		unsigned major;
		unsigned minor;
	
		inline bool operator==(const ServicePack & o) const {
			return (major == o.major && minor == o.minor);
		}
		
		inline bool operator!=(const ServicePack & o) const {
			return !(*this == o);
		}
		
	};
	
	ServicePack ntServicePack;
	
	void load(std::istream & is, const inno_version & version);
	
	inline bool operator==(const WindowsVersion & o) const {
		return (winVersion == o.winVersion
		        && ntServicePack == o.ntServicePack
		        && ntServicePack == o.ntServicePack);
	}
	
	inline bool operator!=(const WindowsVersion & o) const {
		return !(*this == o);
	}
	
	static const WindowsVersion none;
	
};

std::ostream & operator<<(std::ostream & os, const WindowsVersion::Version & svd);
std::ostream & operator<<(std::ostream & os, const WindowsVersion & svd);

#endif // INNOEXTRACT_SETUP_WINDOWSVERSION_HPP
