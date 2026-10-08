#pragma once
namespace hdn::studio {
bool registerPapyrus(RE::BSScript::IVirtualMachine* vm);
void onMessage(SKSE::MessagingInterface::Message* message);
}
