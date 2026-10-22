import { useEffect, useRef, useState } from 'react';

export function PresenterScript() {
  const [script, setScript] = useState(() => {
    try { return sessionStorage.getItem('presenterScript') || ''; } catch { return ''; }
  });
  const [editing, setEditing] = useState(!script);
  const [fontSize, setFontSize] = useState(24);
  const [expanded, setExpanded] = useState(false);
  const dialog = useRef<HTMLDialogElement>(null);
  const [saveFailed, setSaveFailed] = useState(false);

  useEffect(() => {
    try { sessionStorage.setItem('presenterScript', script); setSaveFailed(false); }
    catch { setSaveFailed(true); }
  }, [script]);
  useEffect(() => {
    if (expanded) dialog.current?.showModal();
    else dialog.current?.close();
  }, [expanded]);

  const reader = (large = false) => <div className={`script-reader ${large ? 'script-reader-expanded' : ''}`} style={{ fontSize }} tabIndex={0} role="region" aria-label="Presenter script">
    {script || 'Your script will appear here. Choose Edit script to type or paste your speaking notes.'}
  </div>;
  const sizing = <label className="script-size">Text size
    <select className="input" value={fontSize} onChange={event => setFontSize(Number(event.target.value))}>
      {[18, 24, 30, 36, 44].map(size => <option key={size} value={size}>{size}px</option>)}
    </select>
  </label>;

  return <aside className="card presenter-script" aria-labelledby="presenter-script-title">
    <h2 id="presenter-script-title">Presenter script</h2>
    <p className="script-help">Not sent to viewers. Sharing this window or your entire screen can reveal these notes.</p>
    <div className="toolbar script-toolbar">
      <button className="btn btn-secondary" onClick={() => setEditing(!editing)}>{editing ? 'Read script' : 'Edit script'}</button>
      <button className="btn btn-secondary" onClick={() => setExpanded(true)}>Expand script</button>
      {sizing}
    </div>
    {editing ? <label className="label">Speaking notes<textarea className="input script-editor" style={{ fontSize }} value={script} onChange={event => setScript(event.target.value)} placeholder="Type or paste your presentation script here…" /></label> : reader()}
    <p className="script-help" role="status">{saveFailed ? 'Notes are available for this visit only; browser storage is unavailable.' : 'Saved in this browser tab. Scroll the script independently while presenting.'}</p>
    <dialog ref={dialog} className="script-dialog" aria-labelledby="expanded-script-title" onCancel={() => setExpanded(false)} onClose={() => setExpanded(false)}>
      <header className="toolbar"><h2 id="expanded-script-title">Presenter script</h2>{sizing}<button className="btn btn-primary" autoFocus onClick={() => setExpanded(false)}>Close expanded view</button></header>
      {reader(true)}
    </dialog>
  </aside>;
}
