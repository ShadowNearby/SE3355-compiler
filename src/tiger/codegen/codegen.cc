#include "tiger/codegen/codegen.h"

#include <cassert>
#include <sstream>

extern frame::RegManager *reg_manager;

namespace {

constexpr int maxlen = 1024;

} // namespace

namespace cg {

void CodeGen::Codegen() {
  fs_ = frame_->GetLabel() + "_framesize";
  auto list = new assem::InstrList{};
  for (const auto stm : traces_->GetStmList()->GetList()) {
    stm->Munch(*list, fs_);
  }
  list = frame::ProcEntryExit2(list);
  assem_instr_ = std::make_unique<AssemInstr>(list);
}

void AssemInstr::Print(FILE *out, temp::Map *map) const {
  for (auto instr : instr_list_->GetList())
    instr->Print(out, map);
  fprintf(out, "\n");
}
} // namespace cg

namespace tree {

void SeqStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  left_->Munch(instr_list, fs);
  right_->Munch(instr_list, fs);
}

void LabelStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto instr =
      new assem::LabelInstr{temp::LabelFactory::LabelString(label_), label_};
  instr_list.Append(instr);
}

void JumpStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto instr =
      new assem::OperInstr{"jmp `j0", new temp::TempList{},
                           new temp::TempList{}, new assem::Targets{jumps_}};
  instr_list.Append(instr);
}

void CjumpStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto left = left_->Munch(instr_list, fs);
  auto right = right_->Munch(instr_list, fs);
  instr_list.Append(new assem::OperInstr{"cmpq `s1, `s0", new temp::TempList{},
                                         new temp::TempList{left, right},
                                         nullptr});
  auto targets = new assem::Targets{
      new std::vector<temp::Label *>{true_label_, false_label_}};
  switch (op_) {
  case EQ_OP:
    instr_list.Append(new assem::OperInstr{"je `j0", new temp::TempList{},
                                           new temp::TempList{}, targets});
    break;
  case NE_OP:
    instr_list.Append(new assem::OperInstr{"jne `j0", new temp::TempList{},
                                           new temp::TempList{}, targets});
    break;
  case LT_OP:
  case UGE_OP:
    instr_list.Append(new assem::OperInstr{"jl `j0", new temp::TempList{},
                                           new temp::TempList{}, targets});
    break;
  case GT_OP:
  case ULE_OP:
    instr_list.Append(new assem::OperInstr{"jg `j0", new temp::TempList{},
                                           new temp::TempList{}, targets});
    break;
  case LE_OP:
  case UGT_OP:
    instr_list.Append(new assem::OperInstr{"jle `j0", new temp::TempList{},
                                           new temp::TempList{}, targets});
    break;
  case GE_OP:
  case ULT_OP:
    instr_list.Append(new assem::OperInstr{"jge `j0", new temp::TempList{},
                                           new temp::TempList{}, targets});
    break;
  case REL_OPER_COUNT:
    break;
  }
}

void MoveStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  if (typeid(*dst_) == typeid(tree::MemExp)) {
    auto dst = ((MemExp *)dst_)->exp_->Munch(instr_list, fs);
    if (typeid(*src_) == typeid(tree::ConstExp)) {
      auto const_exp = dynamic_cast<tree::ConstExp *>(src_);
      instr_list.Append(new assem::MoveInstr{
          "movq $" + std::to_string(const_exp->consti_) + ", (`s0)",
          new temp::TempList{}, new temp::TempList{dst}});
      return;
    }
    auto src = src_->Munch(instr_list, fs);
    instr_list.Append(
        new assem::OperInstr{"movq `s0, (`s1)", new temp::TempList{},
                             new temp::TempList{src, dst}, nullptr});
  } else {
    auto dst = dst_->Munch(instr_list, fs);
    if (typeid(*src_) == typeid(tree::ConstExp)) {
      auto const_exp = dynamic_cast<tree::ConstExp *>(src_);
      instr_list.Append(new assem::MoveInstr{
          "movq $" + std::to_string(const_exp->consti_) + ", `d0",
          new temp::TempList{dst}, new temp::TempList{}});
      return;
    }
    auto src = src_->Munch(instr_list, fs);
    instr_list.Append(new assem::MoveInstr(
        "movq `s0, `d0", new temp::TempList{dst}, new temp::TempList{src}));
  }
}

void ExpStm::Munch(assem::InstrList &instr_list, std::string_view fs) {
  exp_->Munch(instr_list, fs);
}

temp::Temp *BinopExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto temp_val = temp::TempFactory::NewTemp();
  auto rax = reg_manager->GetRegister("rax");
  auto rdx = reg_manager->GetRegister("rdx");
  if (op_ == PLUS_OP) {
    if (typeid(*left_) == typeid(tree::ConstExp)) {
      auto right = right_->Munch(instr_list, fs);
      auto const_exp = dynamic_cast<tree::ConstExp *>(left_);
      instr_list.Append(new assem::MoveInstr{"movq `s0, `d0",
                                             new temp::TempList{temp_val},
                                             new temp::TempList{right}});
      instr_list.Append(new assem::OperInstr{
          "addq $" + std::to_string(const_exp->consti_) + ", `d0",
          new temp::TempList{temp_val}, new temp::TempList{temp_val}, nullptr});
      return temp_val;
    }
    if (typeid(*right_) == typeid(tree::ConstExp)) {
      auto left = left_->Munch(instr_list, fs);
      auto const_exp = dynamic_cast<tree::ConstExp *>(right_);
      instr_list.Append(new assem::MoveInstr{"movq `s0, `d0",
                                             new temp::TempList{temp_val},
                                             new temp::TempList{left}});
      instr_list.Append(new assem::OperInstr{
          "addq $" + std::to_string(const_exp->consti_) + ", `d0",
          new temp::TempList{temp_val}, new temp::TempList{temp_val}, nullptr});
      return temp_val;
    }
    auto left = left_->Munch(instr_list, fs);
    auto right = right_->Munch(instr_list, fs);
    instr_list.Append(new assem::MoveInstr{"movq `s0, `d0",
                                           new temp::TempList{temp_val},
                                           new temp::TempList{left}});
    instr_list.Append(
        new assem::OperInstr{"addq `s0, `d0", new temp::TempList{temp_val},
                             new temp::TempList{right, temp_val}, nullptr});
    return temp_val;
  }
  if (op_ == MINUS_OP) {
    auto left = left_->Munch(instr_list, fs);
    auto right = right_->Munch(instr_list, fs);
    instr_list.Append(new assem::MoveInstr{"movq `s0, `d0",
                                           new temp::TempList{temp_val},
                                           new temp::TempList{left}});
    instr_list.Append(
        new assem::OperInstr{"subq `s0, `d0", new temp::TempList{temp_val},
                             new temp::TempList{right, temp_val}, nullptr});
    return temp_val;
  }
  if (op_ == MUL_OP) {
    auto left = left_->Munch(instr_list, fs);
    auto right = right_->Munch(instr_list, fs);
    instr_list.Append(new assem::MoveInstr{
        "movq `s0, `d0", new temp::TempList{rax}, new temp::TempList{left}});
    instr_list.Append(
        new assem::OperInstr{"imulq `s0", new temp::TempList{rax, rdx},
                             new temp::TempList{right, rax}, nullptr});
    instr_list.Append(new assem::MoveInstr{"movq `s0, `d0",
                                           new temp::TempList{temp_val},
                                           new temp::TempList{rax}});
    return temp_val;
  }
  if (op_ == DIV_OP) {
    auto left = left_->Munch(instr_list, fs);
    auto right = right_->Munch(instr_list, fs);
    instr_list.Append(new assem::MoveInstr{
        "movq `s0, `d0", new temp::TempList{rax}, new temp::TempList{left}});
    instr_list.Append(new assem::OperInstr("cqto",
                                           new temp::TempList({rax, rdx}),
                                           new temp::TempList(rax), nullptr));
    instr_list.Append(
        new assem::OperInstr{"idivq `s0", new temp::TempList{rax, rdx},
                             new temp::TempList{right, rax, rdx}, nullptr});
    instr_list.Append(new assem::MoveInstr{"movq `s0, `d0",
                                           new temp::TempList{temp_val},
                                           new temp::TempList{rax}});
    return temp_val;
  }
  return temp_val;
}

temp::Temp *MemExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto res = temp::TempFactory::NewTemp();
  auto exp = exp_->Munch(instr_list, fs);
  instr_list.Append(new assem::OperInstr{"movq (`s0), `d0",
                                         new temp::TempList(res),
                                         new temp::TempList(exp), nullptr});
  return res;
}

temp::Temp *TempExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  if (temp_ != reg_manager->FramePointer()) {
    return temp_;
  }
  auto reg = temp::TempFactory::NewTemp();
  std::stringstream ss;
  ss << "leaq " << fs << "(`s0), `d0";
  instr_list.Append(new assem::OperInstr(
      ss.str(), new temp::TempList(reg),
      new temp::TempList(reg_manager->StackPointer()), nullptr));
  return reg;
}

temp::Temp *EseqExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  stm_->Munch(instr_list, fs);
  return exp_->Munch(instr_list, fs);
}

temp::Temp *NameExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto res = temp::TempFactory::NewTemp();
  instr_list.Append(new assem::OperInstr{
      "leaq " + name_->Name() + "(%rip), `d0", new temp::TempList{res},
      new temp::TempList{}, nullptr});
  return res;
}

temp::Temp *ConstExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto dst = temp::TempFactory::NewTemp();
  auto instr = new assem::OperInstr{
      "movq $" + std::to_string(consti_) + ", `d0", new temp::TempList{dst},
      new temp::TempList{}, nullptr};
  instr_list.Append(instr);
  return dst;
}

temp::Temp *CallExp::Munch(assem::InstrList &instr_list, std::string_view fs) {
  auto arg_regs = args_->MunchArgs(instr_list, fs);
  instr_list.Append(
      new assem::OperInstr("callq " + ((NameExp *)fun_)->name_->Name(),
                           reg_manager->CallerSaves(), arg_regs, nullptr));
  auto return_label = temp::LabelFactory::NewLabel();
  instr_list.Append(new assem::LabelInstr(return_label->Name(), return_label));
  auto arg_num = args_->GetList().size();
  static auto arg_reg_num = reg_manager->ArgRegs()->GetList().size();
  auto arg_stack_num = arg_num > arg_reg_num ? arg_num - arg_reg_num : 0;
  if (arg_stack_num != 0) {
    instr_list.Append(new assem::OperInstr{
        "addq $" + std::to_string(reg_manager->WordSize() * arg_stack_num) +
            ", `d0",
        new temp::TempList{reg_manager->StackPointer()}, new temp::TempList{},
        nullptr});
  }
  return reg_manager->ReturnValue();
}

temp::TempList *ExpList::MunchArgs(assem::InstrList &instr_list,
                                   std::string_view fs) {
  auto res = new temp::TempList{};
  auto arg_regs = reg_manager->ArgRegs();
  auto exp_it = exp_list_.cbegin();
  for (int i = 0;
       i < (int)arg_regs->GetList().size() && exp_it != exp_list_.cend();
       ++i, ++exp_it) {
    auto reg = arg_regs->NthTemp(i);
    auto exp = *exp_it;
    auto temp = exp->Munch(instr_list, fs);
    instr_list.Append(new assem::MoveInstr{
        "movq `s0, `d0", new temp::TempList{reg}, new temp::TempList{temp}});
    res->Append(reg);
  }
  auto exp_it2 = exp_list_.cend();
  exp_it2--;
  exp_it--;
  for (int i = 0; exp_it2 != exp_it; ++i, --exp_it2) {
    auto exp = *exp_it2;
    instr_list.Append(new assem::OperInstr{
        "subq $" + std::to_string(reg_manager->WordSize()) + ", `d0",
        new temp::TempList{reg_manager->StackPointer()}, new temp::TempList{},
        nullptr});
    instr_list.Append(
        new assem::OperInstr{"movq `s0, (`s1)", new temp::TempList{},
                             new temp::TempList{exp->Munch(instr_list, fs),
                                                reg_manager->StackPointer()},
                             nullptr});
  }
  return res;
}

} // namespace tree
