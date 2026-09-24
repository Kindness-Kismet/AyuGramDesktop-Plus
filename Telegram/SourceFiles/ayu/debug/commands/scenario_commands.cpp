#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/debug/debug_login.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_scheduled_section.h"
#include "main/main_session.h"
#include "spellcheck/spellcheck_types.h"
#include "window/window_session_controller.h"

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

enum class Kind {
	Private,
	Contact,
	Blocked,
	Bot,
	Broadcast,
	Discussion,
	Join,
	Restricted,
	Translate,
	Requests,
	Topic,
	Call,
	Business,
	Paid,
};

struct Scenario {
	const char *key;
	const char16_t *name;
	Kind kind;
};

constexpr auto kScenarios = std::array{
	Scenario{ "private", u"01 普通私聊与多行输入", Kind::Private },
	Scenario{ "contact", u"02 陌生人联系人提示", Kind::Contact },
	Scenario{ "blocked", u"03 已屏蔽用户", Kind::Blocked },
	Scenario{ "bot", u"04 机器人启动", Kind::Bot },
	Scenario{ "channel", u"05 只读频道与置顶", Kind::Broadcast },
	Scenario{ "discussion", u"06 频道讨论入口", Kind::Discussion },
	Scenario{ "join", u"07 未加入频道", Kind::Join },
	Scenario{ "restricted", u"08 群聊发送限制", Kind::Restricted },
	Scenario{ "translate", u"09 翻译与置顶组合", Kind::Translate },
	Scenario{ "requests", u"10 加入申请与置顶", Kind::Requests },
	Scenario{ "topic", u"11 话题输入区", Kind::Topic },
	Scenario{ "call", u"12 通话与申请堆叠", Kind::Call },
	Scenario{ "business", u"13 商业机器人提示", Kind::Business },
	Scenario{ "paid", u"14 付费消息提示", Kind::Paid },
};

constexpr auto kFirstPeerId = uint64(810000001);
constexpr auto kFirstMessageId = 2000001;
constexpr auto kTopicRootId = 500;
base::weak_ptr<Main::Session> SeededSession;

[[nodiscard]] bool isUser(Kind kind) {
	return kind == Kind::Private
		|| kind == Kind::Contact
		|| kind == Kind::Blocked
		|| kind == Kind::Bot
		|| kind == Kind::Business
		|| kind == Kind::Paid;
}

[[nodiscard]] PeerId scenarioPeerId(int index) {
	const auto id = kFirstPeerId + index;
	return isUser(kScenarios[index].kind)
		? peerFromUser(UserId(id))
		: peerFromChannel(ChannelId(id));
}

[[nodiscard]] json scenarioList() {
	auto result = json::array();
	for (auto i = 0; i != kScenarios.size(); ++i) {
		result.push_back({
			{ "key", kScenarios[i].key },
			{ "name", QString::fromUtf16(kScenarios[i].name).toStdString() },
			{ "peerId", scenarioPeerId(i).value },
		});
	}
	return result;
}

void initialiseUser(
		not_null<UserData*> user,
		const QString &name) {
	user->setName(name, {}, {}, {});
	user->setAccessHash(0);
	user->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown);
	user->setBarSettings(PeerBarSettings());
	user->setLoadedStatus(PeerData::LoadedStatus::Full);
}

[[nodiscard]] MTPMessage makeMessage(
		not_null<PeerData*> peer,
		PeerId sender,
		int id,
		const QString &text,
		bool pinned,
		bool topic) {
	using Flag = MTPDmessage::Flag;
	using ReplyFlag = MTPDmessageReplyHeader::Flag;
	const auto reply = topic ? MTP_messageReplyHeader(
		MTP_flags(ReplyFlag::f_forum_topic | ReplyFlag::f_reply_to_msg_id),
		MTP_int(kTopicRootId),
		MTPPeer(), MTPMessageFwdHeader(), MTPMessageMedia(),
		MTPint(), MTPstring(), MTPVector<MTPMessageEntity>(),
		MTPint(), MTPint(), MTPstring()) : MTPMessageReplyHeader();
	return MTP_message(
		MTP_flags(Flag::f_from_id
			| (pinned ? Flag::f_pinned : Flag())
			| (topic ? Flag::f_reply_to : Flag())
			| (peer->isBroadcast() ? Flag::f_post : Flag())),
		MTP_int(id), peerToMTP(sender), MTPint(), MTPstring(),
		peerToMTP(peer->id), MTPPeer(), MTPMessageFwdHeader(),
		MTPlong(), MTPlong(), MTPPeer(), reply,
		MTP_int(base::unixtime::now() - 300 + (id % 100)),
		MTP_string(text), MTPMessageMedia(), MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(), MTPint(), MTPint(),
		MTPMessageReplies(), MTPint(), MTPstring(), MTPlong(),
		MTPMessageReactions(), MTPVector<MTPRestrictionReason>(),
		MTPint(), MTPint(), MTPlong(), MTPFactCheck(), MTPint(),
		MTPlong(), MTPSuggestedPost(), MTPint(), MTPstring(),
		MTPRichMessage());
}

void fillHistory(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		int index,
		bool pinned) {
	const auto history = session->data().history(peer);
	history->clearFolder();
	history->addOlderSlice({});
	const auto topic = kScenarios[index].kind == Kind::Topic;
	const auto translating = kScenarios[index].kind == Kind::Translate;
	const auto sender = peer->isBroadcast() ? peer->id
		: peer->isUser() ? peer->id
		: peerFromUser(UserId(kFirstPeerId));
	auto messages = QVector<MTPMessage>();
	auto replyIds = std::vector<MsgId>();
	if (topic) {
		messages.push_back(makeMessage(peer, session->userPeerId(),
			kTopicRootId, u"话题开始：检查独立输入区。"_q, false, false));
	}
	const auto messageCount = translating ? 5 : 6;
	for (auto i = 0; i != messageCount; ++i) {
		const auto text = translating
			? u"Bonjour, voici un exemple de conversation pour vérifier la traduction et la disposition des messages. %1"_q.arg(i + 1)
			: (i == 0 && pinned)
			? u"置顶说明：检查顶部条、正文留白与窄窗换行。"_q
			: u"本地场景消息 %1：用于检查输入区和提示条的布局。"_q.arg(i + 1);
		const auto id = kFirstMessageId + 100 * index + i;
		messages.prepend(makeMessage(peer, sender, id,
			text, pinned && i == 0, topic));
		replyIds.push_back(id);
	}
	history->addNewerSlice(messages);
	history->addNewerSlice({});
	if (topic) {
		peer->forum()->topicFor(kTopicRootId)->replies()
			->setLocalMessagesForDebug(std::move(replyIds));
	}
	history->setUnreadCount(0);
	history->setInboxReadTill(kFirstMessageId + 100 * index + messageCount);
	history->setChatListTimeId(base::unixtime::now() - index);
	if (kScenarios[index].kind == Kind::Join) {
		session->data().setChatPinned(history, FilterId(), true);
	}
	history->updateChatListExistence();
}

void seedScenario(not_null<Main::Session*> session, int index) {
	const auto &spec = kScenarios[index];
	const auto name = QString::fromUtf16(spec.name);
	const auto peer = session->data().peer(scenarioPeerId(index));
	if (const auto user = peer->asUser()) {
		initialiseUser(user, name);
		if (spec.kind == Kind::Contact) {
			user->setBarSettings(PeerBarSetting::AddContact
				| PeerBarSetting::BlockContact);
		} else if (spec.kind == Kind::Blocked) {
			user->setIsBlocked(true);
		} else if (spec.kind == Kind::Bot) {
			user->setBotInfoVersion(1);
			user->botInfo->startToken = u"layout"_q;
			user->botInfo->inited = true;
		} else if (spec.kind == Kind::Business || spec.kind == Kind::Paid) {
			using Flag = MTPDpeerSettings::Flag;
			const auto business = spec.kind == Kind::Business;
			user->setBarSettings(MTP_peerSettings(
				MTP_flags(business
					? (Flag::f_business_bot_id | Flag::f_business_bot_can_reply)
					: Flag::f_charge_paid_message_stars),
				MTPint(), MTPstring(), MTPint(),
				MTP_long(kFirstPeerId + 3), MTP_string("https://t.me/"),
				MTP_long(5), MTPstring(), MTPstring(), MTPint(), MTPint()));
			if (!business) {
				user->setStarsPerMessage(5);
			}
		}
	} else {
		const auto channel = peer->asChannel();
		using Flag = ChannelDataFlag;
		const auto broadcast = spec.kind == Kind::Broadcast
			|| spec.kind == Kind::Discussion || spec.kind == Kind::Join;
		channel->setName(name, {});
		channel->setAccessHash(0);
		channel->setFlags((broadcast ? Flag::Broadcast : Flag::Megagroup)
			| (spec.kind == Kind::Join ? Flag::Left : Flag())
			| (spec.kind == Kind::Discussion ? Flag::HasLink : Flag())
			| (spec.kind == Kind::Topic ? Flag::Forum : Flag()));
		channel->setLoadedStatus(PeerData::LoadedStatus::Full);
		channel->setBarSettings(PeerBarSettings());
		channel->setMembersCount(16);
		channel->setDefaultRestrictions(spec.kind == Kind::Restricted
			? Data::AllSendRestrictions() : ChatRestrictions());
		if (spec.kind == Kind::Discussion) {
			const auto group = session->data().channel(ChannelId(kFirstPeerId + 8));
			channel->setDiscussionLink(group);
		} else if (spec.kind == Kind::Requests || spec.kind == Kind::Call) {
			channel->setAdminRights(ChatAdminRight::InviteByLinkOrAdd | ChatAdminRight::ProcessJoinRequests);
			channel->setPendingRequestsCount(1,
				std::vector<UserId>{ UserId(kFirstPeerId + 1) });
		} else if (spec.kind == Kind::Topic) {
			channel->forum()->applyTopicAdded(kTopicRootId,
				u"话题输入布局"_q, 0x6FB9F0, 0,
				session->userPeerId(), base::unixtime::now() - 600, true);
		}
		if (spec.kind == Kind::Call) {
			channel->setGroupCall(MTP_inputGroupCall(MTP_long(810001), MTPlong()),
				base::unixtime::now() + 3600);
		}
	}
	const auto pinned = spec.kind == Kind::Broadcast
		|| spec.kind == Kind::Discussion || spec.kind == Kind::Translate
		|| spec.kind == Kind::Requests || spec.kind == Kind::Call;
	fillHistory(session, peer, index, pinned);
	if (spec.kind == Kind::Translate) {
		peer->setTranslationDisabled(false);
		session->data().history(peer)->translateOfferFrom({ QLocale::French });
	}
}

[[nodiscard]] Result seedScenarios(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: scenario.seed"_q);
	}
	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) {
		return Result::Err(u"an in-process fake session is required"_q);
	}
	if (SeededSession.get() != session) {
		for (auto i = 0; i != kScenarios.size(); ++i) {
			seedScenario(session, i);
		}
		SeededSession = base::make_weak(session);
	}
	return Result::Ok(Compact(scenarioList()));
}

[[nodiscard]] Result listScenarios(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: scenario.list"_q);
	}
	return Result::Ok(Compact(scenarioList()));
}

[[nodiscard]] Result openScenario(const QStringList &args) {
	if (args.size() != 1 && (args.size() != 3 || args[1] != u"--view"_q)) {
		return Result::Err(u"usage: scenario.open <key> [--view main|alternate|scheduled]"_q);
	}
	const auto view = (args.size() == 3) ? args[2] : u"main"_q;
	if (view != u"main"_q && view != u"alternate"_q && view != u"scheduled"_q) {
		return Result::Err(u"unknown view"_q);
	}
	const auto session = ActiveSession();
	if (!session || SeededSession.get() != session) {
		return Result::Err(u"run scenario.seed in a fake session first"_q);
	}
	const auto controller = session->tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no window controller"_q);
	}
	for (auto i = 0; i != kScenarios.size(); ++i) {
		if (args.front() != QLatin1String(kScenarios[i].key)) {
			continue;
		}
		const auto peer = session->data().peer(scenarioPeerId(i));
		const auto history = session->data().history(peer);
		if (view == u"scheduled"_q) {
			controller->showSection(
				std::make_shared<HistoryView::ScheduledMemento>(history),
				Window::SectionShow::Way::ClearStack);
		} else if (view == u"alternate"_q) {
			controller->showSection(std::make_shared<HistoryView::ChatMemento>(
				HistoryView::ChatViewId{ .history = history }),
				Window::SectionShow::Way::ClearStack);
		} else if (kScenarios[i].kind == Kind::Topic) {
			controller->showTopic(peer->forum()->topicFor(kTopicRootId),
				ShowAtTheEndMsgId, Window::SectionShow::Way::ClearStack);
		} else {
			controller->showPeerHistory(peer,
				Window::SectionShow::Way::ClearStack, ShowAtTheEndMsgId);
		}
		return Result::Ok(Compact({ { "key", kScenarios[i].key },
			{ "peerId", peer->id.value }, { "view", view.toStdString() } }));
	}
	return Result::Err(u"unknown scenario, use scenario.list"_q);
}

} // namespace

const HandlerMap &ScenarioHandlers() {
	static const auto result = HandlerMap{
		{ u"scenario.seed"_q, &seedScenarios },
		{ u"scenario.list"_q, &listScenarios },
		{ u"scenario.open"_q, &openScenario },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
