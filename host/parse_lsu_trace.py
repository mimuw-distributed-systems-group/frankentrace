from collections import defaultdict
from typing import Tuple

from fnmatch import fnmatch

import sys

from pathlib import Path

import pandas as pd
import click


# sigrok-cli -i data_trace_concat.srzip -P uart:rx=D0:baudrate=8000000,arm_itm3 -B arm_itm3=pkg_csv |  pd  -o lsu_trace_concat.pkl
# Assert no overflows:
# pd lsu_trace_concat.pkl -@ ptype==@overflow -e count

#

def phase1(df: pd.DataFrame):
    # pd lsu_trace_concat.pkl -c gid 'val.str.startswith("0xbaba") & ptype.eq(@software) & kind.eq(@1)' -c gid c.cumsum{} -s 'closed=(val=="0xb0bafeed").cumsum()'  -e 'df[gid - closed == 1]' -o pd_filtered.pkl
    df['gid'] = df.val.str.startswith("0xbaba") & df.ptype.eq('software') & df.kind.eq('1')
    df['gid'] = df['gid'].cumsum()
    closed = (df.val == "0xb0bafeed").fillna(0).cumsum()
    # XXX: we're missing some
    # df = df[df.gid - closed == 1]
    df['closed'] = closed
    return df


def phase2(df: pd.DataFrame):
    # pd pd_filtered.pkl  -s 'df.loc[df.ptype==@timestamp, @dts] = df.val' -c dts 'c.astype{float}' -c gid c.astype{int} -c next_ts dts.groupby{gid}.cumsum{}.fillna{method=@bfill}.pad{}.astype{int} ?f *_sa,dts,dwt_ts* -o pd_filtered_timestamped.pkl
    df.loc[df.ptype=='timestamp', 'dts'] = df.val
    df['dts'] = df.dts.astype(float)
    df['gid'] = df['gid'].astype(int)
    df['next_ts'] = df.dts.groupby(df.gid).cumsum().fillna(method='bfill').pad().astype(int)
    df = df[[c for c in df.columns if not fnmatch(c, '*_sa') and not c=='dts' and not fnmatch(c, 'dwt_ts*')]]
    return df


def pc_lsu_split(df: pd.DataFrame) -> Tuple[pd.DataFrame, pd.DataFrame]:
    g = df.groupby('gid')
    is_lsu = lambda df: (df.kind == '4').any()
    # no special marker for now
    is_pc = lambda df: not (df.kind == '4').any()
    return g.filter(is_pc), g.filter(is_lsu)
# pd pc_trace_timestamped.pkl ?f *shifted,*_sa  -@ kind==@pc -c ts next_ts -g ts -e 'val.agg([@first, @nunique])' -c nunique c==1 -s df.columns=[@val,@valid] -o pc_trace.csv

def reformat(df: pd.DataFrame):
    a = df[df['kind'] == '4']
    b = df[df['kind'] == '5']
    mode = df[df['kind'] == '6']
    assert a.val.nunique() == 1
    assert b.val.nunique() == 1
    assert mode.val.nunique() == 1, mode

    # addr emission
    a = a.val.iloc[0]
    a_kind = 'COMP:0 addr_16b'
    b = b.val.iloc[0]
    b_kind = 'COMP:2 addr_16b'
    if mode.val.iloc[0] in ('0x21', '0x22', '0x2c', '0x2e', '0x2d', '0x2f'):
        df.loc[df.kind == a_kind, 'val'] = df[df.kind == a_kind].val.str.replace("0x", a)
        df.loc[df.kind == b_kind, 'val'] = df[df.kind == b_kind].val.str.replace("0x", b)

    return df


def join(df: pd.DataFrame):
    a = df[df['kind'] == '4']
    b = df[df['kind'] == '5']
    mode = df[df['kind'] == '6']
    assert a.val.nunique() == 1, a
    assert b.val.nunique() == 1, b
    # assert mode.val.nunique() == 4, mode

    # XXX: hardcoded mode order: 00 (exc), addr, write, pc
    df = df[df['ptype'] == 'hardware'].groupby(df.gid % 4).apply(lambda x: x.reset_index())
    df.drop(index=0, level='gid', inplace=True)
    if len(df) == 0:
        return

    kinds = df.kind.str.extract(r'COMP:(?P<comp>\d)\s+(?P<data_type>[^:]+)(?::\s*(?P<direction>\w+))?')
    writes = kinds.xs(2, level=0)
    good = kinds['comp'].groupby(level=1).nunique().eq(1).all()
    # assert good, "non-deterministic"
    if not good:
        return

    df['data_type'] = kinds['data_type']
    df.reset_index(level=0, drop=True, inplace=True)
    df = df.pivot(columns='data_type', values=['val', 'next_ts'])
    df.columns = ['_'.join(c) for c in df.columns]
    df['direction'] = writes['direction']
    df['comparator'] = writes['comp']
    df['width'] = (df['val_data'].str.len() - 2)//2
    return df

@click.group('app')
def app(): pass

@app.command('parse')
@click.argument('df_path', type=click.Path(exists=True, dir_okay=False, path_type=Path))
def parse(df_path: Path):
    df = pd.read_pickle(df_path)
    print("stage1")
    df = phase1(df)
    print("stage2")
    df = phase2(df)
    print("pc split")
    pc_df, df = pc_lsu_split(df)
    pc_df.to_pickle(df_path.with_name('pc_trace_timestamped.pkl'))
    print("long format")

    # can fix now?
    df['gid'] -= df['gid'].min()
    df = df.groupby(df.gid).apply(reformat)
    df['pair_id'] = df.gid // 4
    df.to_pickle(df_path.with_name('lsu_trace_reformatted.pkl'))
    df.query("ptype=='hardware'").set_index('gid').to_pickle(df_path.with_name('lsu_trace_long.pkl'))
    print("widen")
    df2 = df.groupby(df.pair_id).apply(join)
    df2.to_pickle(df_path.with_name('lsu_trace_wide.pkl'))

@app.command('sort')
@click.argument('df_path', type=click.Path(exists=True, dir_okay=False, path_type=Path))
def topo_sort(df_path: Path):
    from graphlib import TopologicalSorter
    from collections import Counter
    wide = pd.read_parquet(df_path)
    topological_sorter = TopologicalSorter()

    unmap = {}
    columns = ['val_PC', 'val_addr_16b', 'val_data', 'direction', 'width']
    for gidx, df in wide.groupby(level='pair_id'):
        seen_addr = defaultdict(lambda: 0)
        seen = defaultdict(lambda: 0)
        prev = None
        for i, (idx, row) in enumerate(df.iterrows()):
            addr = row['val_addr_16b']
            datum = tuple(row.loc[columns])
            # datum += ((row['next_ts_PC']-1400*i)//30_000, )
            addr_16 = addr[:6]
            seen[addr_16] += 1
            seen[addr] += 1
            seen[datum] += 1
            datum += (seen[addr_16], seen[addr], seen[datum],)
            unmap[datum] = row
            if prev:
                topological_sorter.add(datum, prev)
            prev = datum

    order = list(topological_sorter.static_order())
    df = pd.DataFrame.from_records(order, columns=columns+['p','na','nd'])
    df.to_pickle(df_path.with_name('lsu_order.pkl'))
    print(df)
    dfu = pd.DataFrame([unmap[x] for x in order])
    dfu.to_pickle(df_path.with_name('lsu_order_unmap.pkl'))
    print(dfu)

dumping = """
cp build/cherry/app.nobl.elf artifacts/hello_world/
pd ~/ngsim/orbuculum/out/lsu_trace_wide.pkl -o artifacts/hello_world/lsu_trace_wide.parquet
pd ~/ngsim/orbuculum/out/lsu_trace_long.pkl -o artifacts/hello_world/lsu_trace_long.parquet
pd ~/ngsim/orbuculum/out/lsu_trace_reformatted.pkl -@ ptype==@software -c kind "c.replace({@1:@start,@4:@comp0,@5:@comp2,@6:@function,@7:@end})"  -o artifacts/hello_world/lsu_trace_meta.parquet


"""

if __name__ == '__main__':
    sys.exit(app())
