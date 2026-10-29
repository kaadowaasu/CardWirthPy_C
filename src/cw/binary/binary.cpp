#include "cw/binary/binary.hpp"

#include "cw/binary/cwscenario.hpp"
#include "cw/binary/cwyado.hpp"

#include <stdexcept>
#include <thread>

namespace cw::binary {

ConvertingThread::~ConvertingThread()
{
    // Python の Thread は破棄時に実行中の処理を暗黙に中断しない。
    // C++側でworkerを外部生成された場合も、所有オブジェクトの寿命を
    // workerより先に終わらせないよう待機する。同一threadからの破棄は
    // joinできないためdetachする。
    if (!worker.joinable()) return;
    if (worker.get_id() == std::this_thread::get_id()) {
        worker.detach();
        return;
    }
    try {
        worker.join();
    } catch (...) {
        // デストラクタから例外を出さず、std::threadのterminateを避ける。
        if (worker.joinable()) worker.detach();
    }
}

void ConvertingThread::initialize(::cw::lib::Value arg_cwdata)
{
    m_cwdata = std::move(arg_cwdata);
    m_path.clear();
    m_complete = false;
}

void ConvertingThread::run()
{
    const auto* object = std::get_if<::cw::lib::ObjectPtr>(&m_cwdata.storage());
    if (!object || !*object) {
        throw std::invalid_argument("ConvertingThread requires conversion data");
    }

    if (const auto yado = std::dynamic_pointer_cast<::cw::binary::cwyado::CWYado>(*object)) {
        m_path = yado->convert();
    } else if (const auto scenario = std::dynamic_pointer_cast<::cw::binary::cwscenario::CWScenario>(*object)) {
        m_path = scenario->convert();
    } else if (const auto unconv = std::dynamic_pointer_cast<::cw::binary::cwyado::UnconvCWYado>(*object)) {
        unconv->convert();
        // Python版でもUnconvCWYado.convert()はNoneを返すためpathは空のまま。
    } else {
        throw std::invalid_argument("unsupported conversion data type");
    }

    // 変換途中の例外ではここへ到達せず、Python版と同じくcomplete=False。
    m_complete = true;
}

std::vector<::cw::lib::String> g_py_all{
    "cwfile", "cwscenario", "cwyado", "xmltemplate", "image", "util",
    "event", "bgimage", "coupon", "summary", "environment"};

void main()
{
}

}

