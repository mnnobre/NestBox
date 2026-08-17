// Teste minimo: so um label, sem grid, sem imagem, sem save.
#include <borealis.hpp>

class MinActivity : public brls::Activity {
 public:
  brls::View* createContentView() override {
    auto* root = new brls::Box(brls::Axis::COLUMN);
    root->setPadding(40, 40, 40, 40);
    auto* l = new brls::Label();
    l->setText("PokeHome - teste minimo");
    l->setFontSize(32);
    root->addView(l);
    root->setFocusable(true);
    root->registerAction("Sair", brls::BUTTON_B,
        [](brls::View*) { brls::Application::quit(); return true; }, false);
    return root;
  }
};

int main(int argc, char* argv[]) {
  if (!brls::Application::init()) return 1;
  brls::Application::createWindow("PokeHome Min");
  brls::Application::setGlobalQuit(false);
  brls::Application::pushActivity(new MinActivity());
  while (brls::Application::mainLoop());
  return 0;
}
