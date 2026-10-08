#pragma once
namespace hdn::studio {
spdlog::logger& logger();
bool registerPapyrus(RE::BSScript::IVirtualMachine* vm);
void onMessage(SKSE::MessagingInterface::Message* message);
}
