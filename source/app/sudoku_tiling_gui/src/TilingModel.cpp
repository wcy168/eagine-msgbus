///
/// Copyright Matus Chochlik.
/// Distributed under the GNU GENERAL PUBLIC LICENSE version 3.
/// See http://www.gnu.org/licenses/gpl-3.0.txt
///

import eagine.core;
import eagine.msgbus;
import std;
#include "TilingModel.hpp"
#include "TilingBackend.hpp"
#include <QVariant>
//------------------------------------------------------------------------------
TilingModel::TilingModel(TilingBackend& backend)
  : QObject{nullptr}
  , eagine::main_ctx_object{"TilngModel", backend}
  , _backend{backend}
  , _bus{"TilngEndpt", *this}
  , _tiling{_bus} {

    eagine::msgbus::setup_connectors(main_context(), _tiling);

    auto& info = _tiling.provided_endpoint_info();
    info.display_name = "sudoku tiling generator";
    info.description = "sudoku tiling solver/generator GUI application";

    eagine::connect<&TilingModel::onHelperAppeared>(this, _tiling.helper_appeared);
    eagine::connect<&TilingModel::onFragmentAdded>(this, _tiling.tiles_generated_4);
    eagine::connect<&TilingModel::onQueueLengthChanged>(
      this, _tiling.queue_length_changed);
}
//------------------------------------------------------------------------------
void TilingModel::initialize() {
    reinitialize(
      app_config().get<int>("msgbus.sudoku.solver.width").value_or(64),
      app_config().get<int>("msgbus.sudoku.solver.height").value_or(64));
    _resetCount = 0;
}
//------------------------------------------------------------------------------
void TilingModel::reinitialize() {
    zero(eagine::cover(_cellCache));
    _resetCount++;

    _tiling.reinitialize(
      {_width, _height},
      eagine::default_sudoku_board_traits<4>().make_generator().generate_medium());
    _backend.onTilingReset();
    emit reinitialized();
}
//------------------------------------------------------------------------------
void TilingModel::reinitialize(int w, int h) {
    if((_width != w) or (_height != h)) {
        _width = w;
        _height = h;
        _cellCache.resize(eagine::std_size(w * h), '\0');
    }
    reinitialize();
}
//------------------------------------------------------------------------------
void TilingModel::update() {
    if(not _tiling.tiling_complete()) {
        _tiling.process_all();
        _tiling.update();
        if(_tiling.solution_timeouted(eagine::unsigned_constant<4>{})) {
            reinitialize(_width, _height);
        }
    }
}
//------------------------------------------------------------------------------
void TilingModel::resetTimeout() {
    _tiling.reset_solution_timeout(eagine::unsigned_constant<4>{});
}
//------------------------------------------------------------------------------
auto TilingModel::getTilingSize() const noexcept -> QSize {
    const auto [x, y] = _tiling.tiling_size(eagine::unsigned_constant<4>{});
    return QSize{x, y};
}
//------------------------------------------------------------------------------
auto TilingModel::getWidth() const noexcept -> int {
    return _width;
}
//------------------------------------------------------------------------------
auto TilingModel::getHeight() const noexcept -> int {
    return _height;
}
//------------------------------------------------------------------------------
auto TilingModel::getCellChar(int row, int column) const noexcept -> char {
    const auto k = eagine::std_size(row * _width + column);
    return _cellCache[k];
}
//------------------------------------------------------------------------------
auto TilingModel::getResetCount() const noexcept -> QVariant {
    return {_resetCount};
}
//------------------------------------------------------------------------------
auto TilingModel::getProgress() const noexcept -> QVariant {
    if(const auto total{_cellCache.size()}) {
        return {_tiling.solution_progress(eagine::unsigned_constant<4>{})};
    }
    return {};
}
//------------------------------------------------------------------------------
auto TilingModel::getKeyCount() const noexcept -> QVariant {
    return {static_cast<qlonglong>(_keyCount)};
}
//------------------------------------------------------------------------------
auto TilingModel::getBoardCount() const noexcept -> QVariant {
    return {static_cast<qlonglong>(_boardCount)};
}
//------------------------------------------------------------------------------
auto TilingModel::isComplete() const noexcept -> bool {
    return _tiling.tiling_complete();
}
//------------------------------------------------------------------------------
auto TilingModel::getUpdatedByHelper(eagine::identifier_t helperId) const noexcept
  -> qlonglong {
    return qlonglong(
      _tiling.updated_by_helper(helperId, eagine::unsigned_constant<4>{}));
}
//------------------------------------------------------------------------------
auto TilingModel::getSolvedByHelper(eagine::identifier_t helperId) const noexcept
  -> qlonglong {
    return qlonglong(
      _tiling.solved_by_helper(helperId, eagine::unsigned_constant<4>{}));
}
//------------------------------------------------------------------------------
auto TilingModel::getCell(int row, int column) const noexcept -> QVariant {
    if(const char c{getCellChar(row, column)}) {
        const char s[2] = {c, '\0'};
        return {static_cast<const char*>(s)};
    }
    return {};
}
//------------------------------------------------------------------------------
void TilingModel::onHelperAppeared(
  const eagine::msgbus::result_context&,
  const eagine::msgbus::sudoku_helper_appeared& appeared) noexcept {
    _backend.onHelperAppeared(appeared.helper_id);
}
//------------------------------------------------------------------------------
void TilingModel::onFragmentAdded(
  eagine::identifier_t helperId,
  const eagine::msgbus::sudoku_tiles<4>& tiles,
  const eagine::msgbus::sudoku_solver_key& fragCoord) noexcept {
    const auto x_y{std::get<std::tuple<int, int>>(fragCoord)};

    _backend.onTileSolved(std::get<0>(x_y), std::get<1>(x_y));
    _backend.onHelperContributed(helperId);

    const auto fragment = tiles.get_fragment(x_y);
    int rmin = _width, rmax = 0;
    int cmin = _height, cmax = 0;
    fragment.for_each_cell(
      [&](const auto& coord, const auto& offs, const auto& glyph) {
          if(auto glyphStr{_traits_4.to_string(glyph)}) {
              const auto column = std::get<0>(coord) + std::get<0>(offs);
              const auto row = std::get<1>(coord) + std::get<1>(offs);
              const auto k = eagine::std_size(row * _width + column);
              if(_cellCache[k] == '\0') {
                  _cellCache[k] = extract(glyphStr).front();
                  rmin = std::min(rmin, row);
                  rmax = std::max(rmax, row);
                  cmin = std::min(cmin, column);
                  cmax = std::max(cmax, column);
              }
          }
      });
    emit fragmentAdded(rmin, cmin, rmax, cmax);
}
//------------------------------------------------------------------------------
void TilingModel::onQueueLengthChanged(
  const eagine::msgbus::sudoku_board_queue_change& change) noexcept {
    if(change.rank == 4) [[likely]] {
        if((_keyCount != change.key_count) or (_boardCount != change.board_count)) {
            _keyCount = change.key_count;
            _boardCount = change.board_count;
            emit queueLengthChanged();
        }
    }
}
//------------------------------------------------------------------------------
