// Netlink RTM_GETSTATS fallback for network stats on non-root Android.
// See btop_netlink.hpp for rationale.
#include "btop_netlink.hpp"

#if defined(__ANDROID__)
#include <cstring>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/if_link.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {
// Build (and cache once) ifindex -> interface name via a SIOCGIFNAME sweep.
// RTM_GETSTATS only returns ifindex, so we need this to map back to names.
const std::unordered_map<int, std::string>& ifindex_names() {
	static std::unordered_map<int, std::string> names = [] {
		std::unordered_map<int, std::string> m;
		int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
		if (fd >= 0) {
			for (int i = 1; i < 300; ++i) {
				struct ifreq ifr{};
				ifr.ifr_ifindex = i;
				if (::ioctl(fd, SIOCGIFNAME, &ifr) == 0)
					m.emplace(i, std::string(ifr.ifr_name));
			}
			::close(fd);
		}
		return m;
	}();
	return names;
}
}  // namespace

std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> android_netlink_stats() {
	std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> out;

	// RTM_GETSTATS dump. IMPORTANT: do NOT bind() the socket (that is what makes
	// iproute2 fail with EACCES here); a plain sendto() to the kernel is allowed.
	int nl = ::socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
	if (nl < 0) return out;

	struct {
		struct nlmsghdr nlh;
		struct if_stats_msg ifs;
	} req{};
	req.nlh.nlmsg_len = sizeof(req);
	req.nlh.nlmsg_type = RTM_GETSTATS;
	req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT | NLM_F_MATCH;
	req.nlh.nlmsg_seq = 1;
	req.nlh.nlmsg_pid = 0;
	req.ifs.family = AF_UNSPEC;
	req.ifs.ifindex = 0;                 // 0 = all interfaces
	req.ifs.filter_mask = IFLA_STATS_LINK_64;

	struct sockaddr_nl kernel{};
	kernel.nl_family = AF_NETLINK;
	if (::sendto(nl, &req, sizeof(req), 0, reinterpret_cast<struct sockaddr*>(&kernel), sizeof(kernel)) < 0) {
		::close(nl);
		return out;
	}

	struct timeval tv{1, 0};
	::setsockopt(nl, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	// Receive the multipart dump until NLMSG_DONE (or timeout/error).
	std::string buf;
	char chunk[65536];
	for (int i = 0; i < 16; ++i) {
		ssize_t n = ::recv(nl, chunk, sizeof(chunk), 0);
		if (n <= 0) break;
		buf.append(chunk, static_cast<size_t>(n));
		// Bail out early once NLMSG_DONE shows up in the buffer.
		size_t scan = 0;
		bool done = false;
		while (scan + sizeof(struct nlmsghdr) <= buf.size()) {
			auto* nh = reinterpret_cast<struct nlmsghdr*>(buf.data() + scan);
			if (nh->nlmsg_len < sizeof(struct nlmsghdr) || scan + nh->nlmsg_len > buf.size()) break;
			if (nh->nlmsg_type == NLMSG_DONE) { done = true; break; }
			scan += NLMSG_ALIGN(nh->nlmsg_len);
		}
		if (done) break;
	}
	::close(nl);

	const auto& names = ifindex_names();

	// Parse each RTM_NEWSTATS message.
	size_t off = 0;
	while (off + sizeof(struct nlmsghdr) <= buf.size()) {
		auto* nh = reinterpret_cast<struct nlmsghdr*>(buf.data() + off);
		if (nh->nlmsg_len < sizeof(struct nlmsghdr) || off + nh->nlmsg_len > buf.size()) break;
		const unsigned msglen = nh->nlmsg_len;

		if (nh->nlmsg_type == RTM_NEWSTATS && msglen >= sizeof(struct nlmsghdr) + sizeof(struct if_stats_msg)) {
			auto* msg = reinterpret_cast<char*>(nh);
			auto* ism = reinterpret_cast<struct if_stats_msg*>(msg + sizeof(struct nlmsghdr));
			const int ifindex = ism->ifindex;

			// Attributes start right after if_stats_msg (4-byte aligned; 16+12=28).
			int attrlen = static_cast<int>(msglen) - 28;
			for (auto* rta = reinterpret_cast<struct rtattr*>(msg + 28); RTA_OK(rta, attrlen); rta = RTA_NEXT(rta, attrlen)) {
				if (rta->rta_type == IFLA_STATS_LINK_64) {
					auto* st = reinterpret_cast<struct rtnl_link_stats64*>(RTA_DATA(rta));
					auto it = names.find(ifindex);
					if (it != names.end())
						out[it->second] = {st->rx_bytes, st->tx_bytes};
					break;
				}
			}
		}
		off += NLMSG_ALIGN(nh->nlmsg_len);
	}

	return out;
}

#else  // !__ANDROID__

// No-op on non-Android (desktop Linux reads /proc/net/dev or /sys directly).
std::unordered_map<std::string, std::pair<std::uint64_t, std::uint64_t>> android_netlink_stats() {
	return {};
}

#endif
