import React from 'react';
import ReactDOM from 'react-dom/client';
import { BrowserRouter, Routes, Route, Navigate } from 'react-router-dom';
import { HostPage } from './pages/HostPage';
import { JoinPage } from './pages/JoinPage';
import './styles/global.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
    <BrowserRouter>
      <Routes>
        <Route path="/host" element={<HostPage />} />
        <Route path="/join" element={<JoinPage />} />
        <Route path="/watch" element={<Navigate to="/join" replace />} />
        {/* Viewers land on root, redirect to join */}
        <Route path="/" element={<Navigate to="/join" replace />} />
      </Routes>
    </BrowserRouter>
);
